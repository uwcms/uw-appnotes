ifndef PETALINUX
$(error "Error: PETALINUX environment variable not set.  Change to the root of your PetaLinux install, and source the settings.sh file")
endif
include apps.common.mk

APP = ipbus

# Add any other object files to this list below
APP_OBJS = ipbus.o Client.o ipacl.o

all: build install

build: $(APP)

$(APP): $(APP_OBJS)
	$(CXX) -std=c++0x $(LDFLAGS) -o $@ $(APP_OBJS) $(LDLIBS) -lmemsvc

clean:
	-rm -f $(APP) *.elf *.gdb *.o

.PHONY: install image

install: $(APP)
	$(TARGETINST) -d $(APP) /bin/$(APP)

%.o: %.cpp
	$(CXX) -std=c++0x -c $(CFLAGS) -o $@ $<

help:
	@echo ""
	@echo "Quick reference for various supported build targets for $(INSTANCE)."
	@echo "----------------------------------------------------"
	@echo "  clean                  clean out build objects"
	@echo "  all                    build $(INSTANCE) and install to rootfs host copy"
	@echo "  build                  build subsystem"
	@echo "  install                install built objects to rootfs host copy"
