#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#ifdef __linux__
#include <stdint.h>
#else
#include <sys/endian.h>
#endif
#include <signal.h>
#include "vmport.h"

#ifdef __linux__
#define GETPROGNAME()	"vmwarectl"
#else
#define GETPROGNAME()	getprogname()
#endif


static void
usage(void)
{
	const char *p;

	p = GETPROGNAME();

	fprintf(stderr,
	    "usage: %s [-d] name ...\n"
	    "       %s [-q] -w name=value ...\n"
	    "       %s [-d] -a\n",
	    p, p, p);
	exit(EXIT_FAILURE);
	/* NOTREACH */
}

static void
vmver_pr(const char *name)
{
	struct vm_backdoor frame;

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = ~VM_MAGIC;
	frame.ecx.word = VM_CMD_GET_VERSION;
	vm_portcmd(&frame);

	printf("%u", frame.eax.word);
}

static void
vmhwver_pr(const char *name)
{
	struct vm_backdoor frame;

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_GET_HWVER;
	vm_portcmd(&frame);

	printf("%u", frame.eax.word);
}

static void
vmmemsize_pr(const char *name)
{
	struct vm_backdoor frame;

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_GET_MEM_SIZE;
	vm_portcmd(&frame);

	printf("%u", frame.eax.word * 1024 * 1024);
}

static void
vmtime_pr(const char *name)
{
	struct vm_backdoor frame;
	uint64_t unixtime;

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_GET_TIME_FULL_LAG;
	vm_portcmd(&frame);
	if (frame.eax.word == VM_MAGIC) {
		unixtime = ((uint64_t)frame.esi.word << 32) + frame.edx.word;
		goto done;
	}
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_GET_TIME_FULL;
	vm_portcmd(&frame);
	if (frame.eax.word == VM_MAGIC) {
		unixtime = ((uint64_t)frame.esi.word << 32) + frame.edx.word;
		goto done;
	}
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_GET_TIME;
	vm_portcmd(&frame);
	unixtime = frame.eax.word;

 done:
	printf("%llu", (unsigned long long)unixtime);
}

static void
vmmhz_pr(const char *name)
{
	struct vm_backdoor frame;

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_GET_SPEED;
	vm_portcmd(&frame);

	printf("%u", frame.eax.word);
}

static void
vmscrsize_pr(const char *name)
{
	struct vm_backdoor frame;

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_GET_SCREEN_SIZE;
	vm_portcmd(&frame);

	printf("%ux%u", frame.eax.part.high, frame.eax.part.low);
}

static void
vmvcpu_pr(const char *name)
{
	struct vm_backdoor frame;
	char *comma;

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_GET_VCPUINFO;
	vm_portcmd(&frame);

	if (frame.eax.word & (1 << VM_VCPUINFO_RESERVED)) {
		printf("reserved");
		return;
	}

	comma = "";
	if (frame.eax.word & (1 << VM_VCPUINFO_SLC64)) {
		printf("%sSLC64", comma);
		comma = ", ";
	}
	if (frame.eax.word & (1 << VM_VCPUINFO_SYNC_VTSCS)) {
		printf("%sVTSCS", comma);
		comma = ", ";
	}
	if (frame.eax.word & (1 << VM_VCPUINFO_HV_REPLAY_OK)) {
		printf("%sHVreplay", comma);
		comma = ", ";
	}
	if (frame.eax.word & (1 << VM_VCPUINFO_LEGACY_X2APIC_OK)) {
		printf("%sLegacyX2APIC", comma);
		comma = ", ";
	}
}

#ifdef __linux__
static inline uint32_t
bswap32(uint32_t x)
{
	return ((x >> 24) & 0xff) +
	    (((x >> 16) & 0xff) << 8) +
	    (((x >> 8) & 0xff) << 16) +
	    ((x & 0xff) << 24);
}
#endif

static void
vmuuid_pr(const char *name)
{
	struct vm_backdoor frame;
	uint32_t uuid[4];

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_GET_BIOS_UUID;
	vm_portcmd(&frame);

	uuid[0] = bswap32(frame.eax.word);
	uuid[1] = bswap32(frame.ebx.word);
	uuid[2] = bswap32(frame.ecx.word);
	uuid[3] = bswap32(frame.edx.word);

	printf("%04x-%02x-%02x-%02x-%02x%04x",
	    uuid[0],
	    uuid[1] >> 16,
	    uuid[1] & 0xffff,
	    uuid[2] >> 16,
	    uuid[2] & 0xffff,
	    uuid[3]);
}

static void
vmmousemode_pr(const char *name)
{
	struct vm_backdoor frame;

	memset(&frame, 0, sizeof(frame));
	frame.ebx.word = 0;
	frame.ecx.word = VM_CMD_ABSPOINTER_STATUS;;
	vm_portcmd(&frame);

	if ((frame.eax.word & 0xffff0000) == 0xffff0000)
		printf("disable");
	else {
		frame.ebx.word = 0;
		frame.ecx.word = VM_CMD_ISMOUSEABSOLUTE;
		vm_portcmd(&frame);

		if (frame.eax.word == 0)
			printf("relative");
		else
			printf("absolute");
	}
}

static int
vmmousemode_wr(const char *name, const char *param)
{
	struct vm_backdoor frame;
	uint32_t mode;

	memset(&frame, 0, sizeof(frame));

	if (strcmp(param, "absolute") == 0)
		mode = VMMOUSE_CMD_REQUEST_ABSOLUTE;
	else if (strcmp(param, "relative") == 0)
		mode = VMMOUSE_CMD_REQUEST_RELATIVE;
	else if (strcmp(param, "disable") == 0)
		mode = VMMOUSE_CMD_DISABLE;
	else {
		printf("%s (ERROR: unknown mode)\n", param);
		return -1;
	}


	if (mode != VMMOUSE_CMD_DISABLE) {
		frame.ebx.word = VMMOUSE_CMD_READ_ID;
		frame.ecx.word = VM_CMD_ABSPOINTER_COMMAND;
		vm_portcmd(&frame);

		frame.ebx.word = 0;
		frame.ecx.word = VM_CMD_ABSPOINTER_STATUS;;
		vm_portcmd(&frame);

		if ((frame.eax.word & 0xffff) == 0) {
			printf("%s (ERROR: no data)\n", param);
			return -1;
		}

		frame.ebx.word = 1;
		frame.ecx.word = VM_CMD_ABSPOINTER_DATA;
		vm_portcmd(&frame);

#define VMMOUSE_VERSION_ID	0x3442554a
		if (frame.eax.word != VMMOUSE_VERSION_ID) {
			printf("%s (ERROR: illegal version id)\n", param);
			return -1;
		}
	}

	frame.ebx.word = mode;
	frame.ecx.word = VM_CMD_ABSPOINTER_COMMAND;
	vm_portcmd(&frame);

	return 0;
}

static struct field {
	const char *name;
	void (*prfunc)(const char *name);
	int (*wrfunc)(const char *name, const char *param);
	const char *description;
} fields[] = {
	/* name				readfunc	writefunc		description				*/
	{ "vmware.host.version",	vmver_pr,	NULL,			"VMware version"			},
	{ "vmware.host.unixtime",	vmtime_pr,	NULL,			"Host time"				},
	{ "vmware.host.mhz",		vmmhz_pr,	NULL,			"Host Speed (MHz)"			},
	{ "vmware.host.screensize",	vmscrsize_pr,	NULL,			"Host ScreenSize"			},
	{ "vmware.version",		vmhwver_pr,	NULL,			"VMware Virtual Hardware version"	},
	{ "vmware.memsize",		vmmemsize_pr,	NULL,			"Memory size"				},
	{ "vmware.vcpuinfo",		vmvcpu_pr,	NULL,			"VCPU feature"				},
	{ "vmware.uuid",		vmuuid_pr,	NULL,			"BIOS UUID"				},
	{ "vmware.mouse.mode",		vmmousemode_pr,	vmmousemode_wr,		"\"disable\", \"absolute\" or \"relative\" settable"	},
	{ .name = NULL }
};

static int
field_writable(const char *name)
{
	struct field *f;

	for (f = fields; f->name != NULL; f++) {
		if (strcmp(name, f->name) == 0) {
			if (f->wrfunc == NULL)
				return 1;
			return 0;
		}
	}
	return -1;
}

static int
field_write(const char *name, const char *value)
{
	struct field *f;

	for (f = fields; f->name != NULL; f++) {
		if (strcmp(name, f->name) == 0) {
			if (f->wrfunc == NULL) {
				fprintf(stderr, "%s: Operation not permitted\n", name);
				return -1;
			}
			return f->wrfunc(name, value);
		}
	}
	fprintf(stderr, "name '%s' is invalid\n", name);
	return -1;
}

static int
field_readable(const char *name)
{
	struct field *f;

	for (f = fields; f->name != NULL; f++) {
		if (strcmp(name, f->name) == 0) {
			if (f->prfunc == NULL)
				return 1;
			return 0;
		}
	}
	return -1;
}

static int
field_read(const char *name)
{
	struct field *f;

	for (f = fields; f->name != NULL; f++) {
		if (strcmp(name, f->name) == 0) {
			f->prfunc(name);
			break;
		}
	}

	if (f->name == NULL) {
		fprintf(stderr, "name '%s' is invalid\n", name);
		return -1;
	}

	return 0;

}

static int
field_desc(const char *name)
{
	struct field *f;

	for (f = fields; f->name != NULL; f++) {
		if (strcmp(name, f->name) == 0) {
			printf("%s", f->description);
			break;
		}
	}

	if (f->name == NULL) {
		fprintf(stderr, "name '%s' is invalid\n", name);
		return -1;
	}

	return 0;
}

static int
field_readall(int dflag)
{
	struct field *f;

	for (f = fields; f->name != NULL; f++) {
		if (dflag) {
			printf("%s: %s\n", f->name, f->description);
		} else {
			printf("%s = ", f->name);
			f->prfunc(f->name);
			printf("\n");
		}
	}

	return 0;
}

static void
eval(char *str, int writable, int dflag, int quiet, int rawflag)
{
	char *value;
	char *name;
	int namelen, rc;

	value = index(str, '=');
	if ((value != NULL) && (writable == 0)) {
		fprintf(stderr, "%s: Must specify -w to set variables\n",
		    GETPROGNAME());
		return;
	}

	if (value != NULL) {
		namelen = value - str;
		name = malloc(namelen + 1);
		if (name == NULL) {
			fprintf(stderr, "%s: cannot allocate memory\n",
			    GETPROGNAME());
			return;
		}
		memcpy(name, str, namelen);
		name[namelen] = '\0';
		value++;
	} else {
		name = str;
	}

	if (value == NULL) {
		rc = field_readable(name);
		if (rc > 0) {
			fprintf(stderr, "%s: Operation not permitted\n", name);
		} else if (rc < 0) {
			fprintf(stderr, "name '%s' is invalid\n", name);
		} else {
			if (dflag) {
				if (!rawflag)
					printf("%s: ", name);
				field_desc(name);
			} else {
				if (!rawflag)
					printf("%s = ", name);
				field_read(name);
			}
			if (!rawflag)
				printf("\n");
		}
	} else {
		rc = field_writable(name);
		if (rc > 0) {
			fprintf(stderr, "%s: Operation not permitted\n", name);
		} else if (rc < 0) {
			fprintf(stderr, "name '%s' is invalid\n", name);
		} else {
			if (dflag) {
				printf("%s: ", name);
				field_desc(name);
			} else {
				if (!quiet && !rawflag) {
					printf("%s: ", name);
					field_read(name);
					printf(" -> ");
				}
				if (field_write(name, value) == 0) {
					if (!quiet) {
						field_read(name);
						if (!rawflag)
							printf("\n");
					}
				}
			}
		}
	}

	if (value != NULL)
		free(name);

	return;
}

static void
no_vmware(void)
{
	fprintf(stderr, "VMWare not detected\n");
	exit(EXIT_FAILURE);
}

static void
sighandler(int signo)
{
	no_vmware();
}

int
main(int argc, char *argv[])
{
	extern char *optarg;
	extern int optind;
	int ch, i;
	int aflag, dflag, qflag, rflag, wflag;

	aflag = dflag = qflag = rflag = wflag = 0;
	while ((ch = getopt(argc, argv, "adqrw")) != -1) {
		switch (ch) {
		case 'a':
			aflag = 1;
			break;
		case 'd':
			dflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'r':
			rflag = 1;
			break;
		case 'w':
			wflag = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (aflag && wflag)
		usage();

	if (!aflag && argc == 0)
		usage();

	/* check vmware */
	signal(SIGSEGV, sighandler);
	signal(SIGBUS, sighandler);
	signal(SIGILL, sighandler);
	if (vm_probe() != 0)
		no_vmware();
	signal(SIGSEGV, SIG_DFL);
	signal(SIGBUS, SIG_DFL);
	signal(SIGILL, SIG_DFL);


	if (aflag)
		return field_readall(dflag);

	for (i = 0; i < argc; i++)
		eval(argv[i], wflag, dflag, qflag, rflag);

	return 0;
}
