#include "Client.h"

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <libmemsvc.h>

#define MAX_OBUF (512*1024) 

static bool memsvc_inited = false;
static memsvc_handle_t memsvc;

bool Client::write_ready()
{
	return obuf.size();
}

bool Client::read_ready()
{
	return obuf.size() < MAX_OBUF; // Accept no input when too much output is unread.
}

std::deque<uint32_t> str2vec(const std::string str)
{
	const char *data = str.data();
	std::deque<uint32_t> vec;
	for (int off = 0, strsize = str.size(); off < strsize; off+=4)
		vec.push_back(*reinterpret_cast<const uint32_t*>(data+off));
	return vec;
}

std::string vec2str(const std::deque<uint32_t> vec)
{
	std::string str;
	str.reserve(4*vec.size());
	for (auto it = vec.begin(), eit = vec.end(); it != eit; ++it) {
		uint32_t val = *it;
		str.append(std::string(reinterpret_cast<char*>(&val), 4));
	}
	return str;
}

bool Client::run_io()
{
	// Raw Network Read
	char buf[128];
	ssize_t readcount = recv(this->fd, buf, 128, MSG_DONTWAIT);
	if (readcount < 0 && errno != EAGAIN)
		return false; // Error or disconnect.
	if (readcount)
		this->ibuf += std::string(buf, readcount);

	/* Extract Protocol Frames
	 *
	 * IPbus TCP appears to use a uint32 length-prefixed message format to wrap
	 * the equivalent of IPbus UDP packets.
	 */
	if (this->ibuf.size()) {
		const char *ibuf_data = this->ibuf.data();
		uint32_t frame_size = ntohl(*reinterpret_cast<const uint32_t*>(ibuf_data));
		if (frame_size >= this->ibuf.size()-4) {
			// There is a protocol frame available.  Dispatch it for processing.
			std::string framedata = this->ibuf.substr(4,frame_size);
			this->ibuf = this->ibuf.erase(0,4+frame_size);

			std::deque<uint32_t> req = str2vec(framedata);
			std::deque<uint32_t> rsp;
			this->process_frame(req, rsp);
			frame_size = htonl(rsp.size()*4);
			this->obuf += std::string(reinterpret_cast<const char *>(&frame_size), 4) + vec2str(rsp);
		}
	}

	if (this->obuf.size()) {
		ssize_t writecount = send(this->fd, this->obuf.data(), this->obuf.size(), MSG_DONTWAIT|MSG_NOSIGNAL);
		if (readcount < 0 && errno != EAGAIN)
			return false; // Error. 
		this->obuf = this->obuf.erase(0, writecount);
	}
	return true;
}

void endian_swap(std::deque<uint32_t> &vec)
{
	for (auto it = vec.begin(), eit = vec.end(); it != eit; ++it)
		*it = __builtin_bswap32(*it);
}

void Client::process_frame(std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (!memsvc_inited) {
		if (memsvc_open(&memsvc) != 0) {
			fprintf(stderr, "Unable to connect to memory service: %s", memsvc_get_last_error(memsvc));
			exit(1);
		}
		memsvc_inited = true;
	}

	response.clear();
	if (request.size() < 1)
		return; // Under minimum processable size.

	bool reverse_endian = false;

	uint32_t packet_header = request.front(); request.pop_front();

	if ((packet_header & 0xff0000f0) == 0x200000f0) {
		// Standard endian, valid version
	}
	else if ((packet_header & 0x0f0000ff) == 0x0f000020) {
		// Reverse endian, valid version
		reverse_endian = true;
		endian_swap(request);
		packet_header = __builtin_bswap32(packet_header);
	}
	else {
		return; // version or endian mismatch
	}

	//uint16_t packet_id = (packet_header >> 16) & 0xffff;
	uint8_t packet_type = packet_header & 0x0f;

	if (packet_type == 0x0) {
		// Control Packet
		process_control_packet(request, response);
	}
	else if (packet_type == 0x1) {
		// Status Packet
		process_status_packet(request, response);
	}
	else if (packet_type == 0x2) {
		// Resend Request Packet
		// Unsupported.  TCP should make this unnecessary.
		return;
	}
	else {
		// Unknown Packet
		return;
	}

	if (!response.size()) {
		return; // Packet was ignored by processor.  Ignore it as well.
	}

	response.push_front(packet_header);

	if (reverse_endian)
		endian_swap(response);
}

void Client::process_control_packet(std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (request.size() < 1) {
		return; // Missing Header.
	}
	
	while (request.size()) {
		IPBusTxnHdr transaction_header(request.front());
		request.pop_front();

		if (transaction_header.protocol_version != 2) {
			response.clear();
			return; // Invalid. Drop packet.
		}

		if (transaction_header.info_code != 0xf) {
			response.clear();
			return; // Invalid. Drop packet.
		}

		switch (transaction_header.type_id) {
			case 0x0: this->process_read_txn(transaction_header, request, response); break;
			case 0x1: this->process_write_txn(transaction_header, request, response); break;
			case 0x2: this->process_niread_txn(transaction_header, request, response); break;
			case 0x3: this->process_niwrite_txn(transaction_header, request, response); break;
			case 0x4: this->process_rmwbits_txn(transaction_header, request, response); break;
			case 0x5: this->process_rmwsum_txn(transaction_header, request, response); break;
			case 0x6: this->process_cfgread_txn(transaction_header, request, response); break;
			case 0x7: this->process_cfgwrite_txn(transaction_header, request, response); break;
			default:
					  transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
					  response.push_back(transaction_header.serialize());
		}
	};
}

void Client::process_read_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (request.size() < 1) {
		// Assumption: An error returns a transaction header without a body.
		transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
		response.push_back(transaction_header.serialize());
		return;
	}
	uint32_t base_addr = request.front(); request.pop_front();

	uint32_t data[transaction_header.words];
	if (memsvc_read(memsvc, base_addr, transaction_header.words, data) == 0) {
		transaction_header.info_code = IPBusTxnHdr::SUCCESS;
		response.push_back(transaction_header.serialize());
		for (int i = 0; i < transaction_header.words; ++i)
			response.push_back(data[i]);
	}
	else {
		transaction_header.info_code = IPBusTxnHdr::BUSERR_READ;
		response.push_back(transaction_header.serialize());
	}
}

void Client::process_write_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (request.size() < 1u+transaction_header.words) {
		// Assumption: An error returns a transaction header without a body.
		transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
		response.push_back(transaction_header.serialize());
		return;
	}
	uint32_t base_addr = request.front(); request.pop_front();

	uint32_t data[transaction_header.words];
	for (int i = 0; i < transaction_header.words; ++i) {
		if (!request.size()) {
			transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
			response.push_back(transaction_header.serialize());
			return;
		}
		data[i] = request.front(); request.pop_front();
	}
	if (memsvc_write(memsvc, base_addr, transaction_header.words, data) == 0) {
		transaction_header.info_code = IPBusTxnHdr::SUCCESS;
		response.push_back(transaction_header.serialize());
	}
	else {
		transaction_header.info_code = IPBusTxnHdr::BUSERR_READ;
		response.push_back(transaction_header.serialize());
	}
}

void Client::process_niread_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (request.size() < 1) {
		// Assumption: An error returns a transaction header without a body.
		transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
		response.push_back(transaction_header.serialize());
		return;
	}
	uint32_t base_addr = request.front(); request.pop_front();

	std::deque<uint32_t> rspdata;
	for (int i = 0; i < transaction_header.words; ++i) {
		uint32_t data;
		if (memsvc_read(memsvc, base_addr, 1, &data) == 0) {
			rspdata.push_back(data);
		}
		else {
			transaction_header.info_code = IPBusTxnHdr::BUSERR_READ;
			response.push_back(transaction_header.serialize());
			return;
		}
	}

	transaction_header.info_code = IPBusTxnHdr::SUCCESS;
	response.push_back(transaction_header.serialize());
	response.insert(response.end(), rspdata.begin(), rspdata.end());
}

void Client::process_niwrite_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (request.size() < 1u+transaction_header.words) {
		// Assumption: An error returns a transaction header without a body.
		transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
		response.push_back(transaction_header.serialize());
		return;
	}
	uint32_t base_addr = request.front(); request.pop_front();

	for (int i = 0; i < transaction_header.words; ++i) {
		uint32_t data = request.front(); request.pop_front();
		if (memsvc_write(memsvc, base_addr, 1, &data) != 0) {
			// Failed!
			transaction_header.info_code = IPBusTxnHdr::BUSERR_WRITE;
			response.push_back(transaction_header.serialize());
			return;
		}
	}

	transaction_header.info_code = IPBusTxnHdr::SUCCESS;
	response.push_back(transaction_header.serialize());
}

void Client::process_rmwbits_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (transaction_header.words != 1 || request.size() < 3) {
		// Assumption: An error returns a transaction header without a body.
		transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
		response.push_back(transaction_header.serialize());
		return;
	}
	uint32_t base_addr = request.front(); request.pop_front();
	uint32_t and_term = request.front(); request.pop_front();
	uint32_t or_term = request.front(); request.pop_front();

	uint32_t predata;
	if (memsvc_read(memsvc, base_addr, 1, &predata) != 0) {
		// Failed!
		transaction_header.info_code = IPBusTxnHdr::BUSERR_READ;
		response.push_back(transaction_header.serialize());
		return;
	}
	uint32_t data = (predata & and_term) | or_term;
	if (memsvc_write(memsvc, base_addr, 1, &data) != 0) {
		// Failed!
		transaction_header.info_code = IPBusTxnHdr::BUSERR_WRITE;
		response.push_back(transaction_header.serialize());
		return;
	}
	transaction_header.info_code = IPBusTxnHdr::SUCCESS;
	response.push_back(transaction_header.serialize());
	response.push_back(predata);
}

void Client::process_rmwsum_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (transaction_header.words != 1 || request.size() < 2) {
		// Assumption: An error returns a transaction header without a body.
		transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
		response.push_back(transaction_header.serialize());
		return;
	}
	uint32_t base_addr = request.front(); request.pop_front();
	uint32_t addend = request.front(); request.pop_front();

	uint32_t predata;
	if (memsvc_read(memsvc, base_addr, 1, &predata) != 0) {
		// Failed!
		transaction_header.info_code = IPBusTxnHdr::BUSERR_READ;
		response.push_back(transaction_header.serialize());
		return;
	}
	uint32_t data = predata + addend;
	if (memsvc_write(memsvc, base_addr, 1, &data) != 0) {
		// Failed!
		transaction_header.info_code = IPBusTxnHdr::BUSERR_WRITE;
		response.push_back(transaction_header.serialize());
		return;
	}
	transaction_header.info_code = IPBusTxnHdr::SUCCESS;
	response.push_back(transaction_header.serialize());
	response.push_back(predata);
}

void Client::process_cfgread_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (request.size() < 1) {
		// Assumption: An error returns a transaction header without a body.
		transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
		response.push_back(transaction_header.serialize());
		return;
	}
	//uint32_t base_addr = request.front();
	request.pop_front();

	// We don't implement the config register space.  Read 0.

	transaction_header.info_code = IPBusTxnHdr::SUCCESS;
	response.push_back(transaction_header.serialize());
	for (int i = 0; i < transaction_header.words; ++i)
		response.push_back(0);
}

void Client::process_cfgwrite_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	if (request.size() < 1u+transaction_header.words) {
		// Assumption: An error returns a transaction header without a body.
		transaction_header.info_code = IPBusTxnHdr::BAD_HEADER;
		response.push_back(transaction_header.serialize());
		return;
	}
	// uint32_t base_addr = request.front();
	request.pop_front();

	// We don't implement the config register space.  Ignore write.
	for (int i = 0; i < transaction_header.words; ++i)
		response.pop_front();

	transaction_header.info_code = IPBusTxnHdr::SUCCESS;
	response.push_back(transaction_header.serialize());
}

void Client::process_status_packet(std::deque<uint32_t> &request, std::deque<uint32_t> &response)
{
	// Not implemented.  All memory access tests pass without this functionality.
}
