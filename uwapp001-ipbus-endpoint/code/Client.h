#ifndef __CLIENT_H
#define __CLIENT_H

#include <stdint.h>
#include <string>
#include <deque>

class IPBusTxnHdr {
	public:
		uint16_t transaction_id; // field 2
		uint8_t protocol_version; // field 1
		uint8_t words;
		uint8_t type_id;
		uint8_t info_code;

		static const int SUCCESS          = 0;
		static const int BAD_HEADER       = 1;
		static const int BUSERR_READ      = 4;
		static const int BUSERR_WRITE     = 5;
		static const int BUSTIMEOUT_READ  = 6;
		static const int BUSTIMEOUT_WRITE = 7;
		static const int REQUEST          = 0xF;

		IPBusTxnHdr(uint32_t transaction_header = 0) {
			this->protocol_version = (transaction_header >> 28) & 0x0f;
			this->transaction_id   = (transaction_header >> 16) & 0x0fff;
			this->words            = (transaction_header >> 8)  & 0xff;
			this->type_id          = (transaction_header >> 4)  & 0x0f;
			this->info_code        = (transaction_header >> 0)  & 0x0f;
		};
		uint32_t serialize() const {
			return (
					((this->protocol_version & 0x0f)   << 28) |
					((this->transaction_id   & 0x0fff) << 16) |
					((this->words            & 0xff)   <<  8) |
					((this->type_id          & 0x0f)   <<  4) |
					((this->info_code        & 0x0f)   <<  0) );
		};
};

class Client {
	protected:
		std::string ibuf;
		std::string obuf;

	public:
		const int fd;
		Client(int fd) : fd(fd) { };

		bool write_ready();
		bool read_ready();
		bool run_io(); // return false to disconnect

	protected:
		void process_frame(std::deque<uint32_t> &request, std::deque<uint32_t> &response);
		void process_control_packet(std::deque<uint32_t> &request, std::deque<uint32_t> &response);
		void process_read_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response);
		void process_write_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response);
		void process_niread_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response);
		void process_niwrite_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response);
		void process_rmwbits_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response);
		void process_rmwsum_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response);
		void process_cfgread_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response);
		void process_cfgwrite_txn(IPBusTxnHdr transaction_header, std::deque<uint32_t> &request, std::deque<uint32_t> &response);

		void process_status_packet(std::deque<uint32_t> &request, std::deque<uint32_t> &response);
};

#endif
