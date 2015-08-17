SUMMARY
=======
vmwarectl - get vmware info and set mouse mode

INSTALL
=======
	% make
	cc -o vmwarectl -O vmwarectl.c vmport.c


HOW TO USE
----------
	% vmwarectl -a
	vmware.host.version = 6
	vmware.host.unixtime = 1439797316
	vmware.host.mhz = 3399
	vmware.host.screensize = 65535x65535
	vmware.version = 4
	vmware.memsize = 1073741824
	vmware.vcpuinfo = VTSCS, LegacyX2APIC
	vmware.uuid = 564de168-ffbf-6bf-5a05-ece21a2a88ac
	vmware.mouse.mode = disable


	# set vmware mouse mode absolute (seamless mouse mode without X)
	% vmwarectl -w vmware.mouse.mode=absolute
	vmware.mouse.mode: disable -> absolute

