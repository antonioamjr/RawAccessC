/*
						S1Search Research 
	Raw Device Access: Reading and Writing directly on a SSD
*/

//==========================================================
// Includes
//
#include <inttypes.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>

//==========================================================
// Constants
//
#define MAX_DEVICE_NAME_SIZE 64
#define WHITE_SPACE " \t\n\r"

// Linux has removed O_DIRECT, but not its functionality.
#ifndef O_DIRECT
#define O_DIRECT 040000 // the leading 0 is necessary - this is octal
#endif

//==========================================================
// Typedefs
//
typedef struct _device {
	const char* name;
	uint64_t num_large_blocks;
	uint64_t num_read_offsets;
	uint32_t min_op_bytes;
	uint32_t read_bytes;
} device;

const char* const SCHEDULER_MODES[] = {
	"noop",
	"cfq"
};

//==========================================================
// Globals
//
static char g_device_name[MAX_DEVICE_NAME_SIZE];
static uint32_t g_scheduler_mode = 0;
static FILE* g_output_file;
static device* g_device;

//==========================================================
// Forward Declarations
//
static void	set_schedulers();
static inline uint8_t* align_4096(uint8_t* stack_buffer);
static inline uint8_t* cf_valloc(size_t size);
static bool config_parse_device_name(char* device);
static bool	configure(int argc, char* argv[]);
static int fd_get(device* p_device);
//static bool	discover_num_blocks(device* p_device);
static uint64_t read_from_device(device* p_device, uint64_t offset,
					uint32_t size, uint8_t* p_buffer);
static uint64_t write_to_device(device* p_device, uint64_t offset,
					uint32_t size, uint8_t* p_buffer);

//==========================================================
// Main
//
int main(int argc, char* argv[]) {
	fprintf(stdout, "\n=> Raw Device Access - direct IO test\n");

	if (! configure(argc, argv)){
		exit(-1);
	}

	set_schedulers();



	fclose(g_output_file);
	return 0;
}

//==========================================================
// Helpers
//

//------------------------------------------------
// Parse device names parameter.
//
static bool config_parse_device_name(char *p_device_name){
	if (strlen(p_device_name) > MAX_DEVICE_NAME_SIZE){
		return false;
	}
	
	strcpy(g_device_name, p_device_name);
	device *dev;
	dev->name = g_device_name;
	g_device = dev;

	return true;
}

//------------------------------------------------
// Create Output File.
//
static bool config_out_file(char *fileName){
	FILE* out = fopen(fileName, "w");

	if (! out) {
		return false;
	}
	else{
		g_output_file = out;
		printf("-> Output file created. Access it when done.\n");
		fprintf(g_output_file, "\n=> Raw Device Access with C Language - output file \n\n");
	}
	return true;
}

//------------------------------------------------
// Set run parameters.
//
static bool	configure(int argc, char* argv[]){
	if (argc != 3){
		printf("=> ERROR: Wrong number of arguments!\nUsage: ./raw device resultfile.\n");
		return false;
	}

	if (! config_parse_device_name(argv[1])){
		printf("=> ERROR: parsing device name.\n");
		return false;
	}

	if (! config_out_file(argv[2])){
		printf("=> ERROR: Couldn't create output file: %s\n", argv[1]);
		return false;
	}

	fprintf(g_output_file,"-> Configuration was a success!\n");
	fprintf(g_output_file,"-> Device name: %s\n", g_device->name);
	return true;
}

//------------------------------------------------
// Get a safe file descriptor for a device.
//
static int fd_get(device* p_device) {
	int fd = -1;

	fd = open(p_device->name, O_DIRECT | O_RDWR, S_IRUSR | S_IWUSR);

	if (fd == -1){
		fprintf(g_output_file, "=> ERROR: Couldn't open device %s\n", p_device->name);
	}

	return fd;
}

//------------------------------------------------
// Discover device storage capacity.
//
/*static bool discover_num_blocks(device* p_device) {
	int fd = fd_get(p_device);

	if (fd == -1) {
		return false;
	}

	uint64_t device_bytes = 0;

	ioctl(fd, BLKGETSIZE64, &device_bytes);
	p_device->num_large_blocks = device_bytes / g_large_block_ops_bytes;
	p_device->min_op_bytes = discover_min_op_bytes(fd, p_device->name);
	fd_put(p_device, fd);

	if (! (p_device->num_large_blocks && p_device->min_op_bytes)) {
		return false;
	}

	uint64_t num_min_op_blocks =
		(p_device->num_large_blocks * g_large_block_ops_bytes) /
			p_device->min_op_bytes;

	uint64_t read_req_min_op_blocks =
		(g_record_bytes + p_device->min_op_bytes - 1) / p_device->min_op_bytes;

	p_device->num_read_offsets = num_min_op_blocks - read_req_min_op_blocks + 1;
	p_device->read_bytes = read_req_min_op_blocks * p_device->min_op_bytes;

	fprintf(stdout, "%s size = %" PRIu64 " bytes, %" PRIu64 " large blocks, "
		"%" PRIu64 " %" PRIu32 "-byte blocks, reads are %" PRIu32 " bytes\n",
			p_device->name, device_bytes, p_device->num_large_blocks,
			num_min_op_blocks, p_device->min_op_bytes, p_device->read_bytes);

	return true;
}*/


//------------------------------------------------
// Do one device read operation.
//
static uint64_t read_from_device(device* p_device, uint64_t offset,
		uint32_t size, uint8_t* p_buffer) {
	int fd = fd_get(p_device);

	if (fd == -1) {
		return -1;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			read(fd, p_buffer, size) != (ssize_t)size) {
		close(fd);
		fprintf(stdout, "ERROR: seek & read\n");
		return -1;
	}

	//uint64_t stop_ns = cf_getns();
	//fd_put(p_device, fd);
	//return stop_ns;
	return 0;
}

//------------------------------------------------
// Do one device write operation.
//
static uint64_t write_to_device(device* p_device, uint64_t offset,
		uint32_t size, uint8_t* p_buffer) {
	int fd = fd_get(p_device);

	if (fd == -1) {
		return -1;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			write(fd, p_buffer, size) != (ssize_t)size) {
		close(fd);
		fprintf(stdout, "ERROR: seek & write\n");
		return -1;
	}

	//uint64_t stop_ns = cf_getns();
	//fd_put(p_device, fd);
	//return stop_ns;
	return 0;
}

//------------------------------------------------
// Set devices' system block schedulers.
//
static void set_schedulers() {
	const char* mode = SCHEDULER_MODES[g_scheduler_mode];
	size_t mode_length = strlen(mode);
	
	const char* device_name = g_device_name;
	const char* p_slash = strrchr(device_name, '/');
	const char* device_tag = p_slash ? p_slash + 1 : device_name;

	char scheduler_file_name[128];

	strcpy(scheduler_file_name, "/sys/block/");
	strcat(scheduler_file_name, device_tag);
	strcat(scheduler_file_name, "/queue/scheduler");

	FILE* scheduler_file = fopen(scheduler_file_name, "w");

	if (! scheduler_file) {
		fprintf(stdout, "ERROR: couldn't open %s\n", scheduler_file_name);
	}

	else if (fwrite(mode, mode_length, 1, scheduler_file) != 1) {
		fprintf(stdout, "ERROR: writing %s to %s\n", mode,
			scheduler_file_name);
	}
	else{
		fclose(scheduler_file);
	}
}

//------------------------------------------------
// Check time differences.
//
static inline uint64_t safe_delta_ns(uint64_t start_ns, uint64_t stop_ns) {
	return start_ns > stop_ns ? 0 : stop_ns - start_ns;
}

//------------------------------------------------
// Align stack-allocated memory.
//
static inline uint8_t* align_4096(uint8_t* stack_buffer) {
	return (uint8_t*)(((uint64_t)stack_buffer + 4095) & ~4095ULL);
}

//------------------------------------------------
// Aligned memory allocation.
//
static inline uint8_t* cf_valloc(size_t size) {
	void* pv;
	return posix_memalign(&pv, 4096, size) == 0 ? (uint8_t*)pv : 0;
}