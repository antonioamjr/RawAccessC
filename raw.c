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

#ifdef linux
#include <linux/fs.h>
#endif

//==========================================================
// Constants
//
#define MAX_DEVICE_NAME_SIZE 64
#define WHITE_SPACE " \t\n\r"

// Linux has removed O_DIRECT, but not its functionality.
#ifndef O_DIRECT
#define O_DIRECT 040000 // the leading 0 is necessary - this is octal
#endif

const uint32_t LO_IO_MIN_SIZE = 512;
const uint32_t HI_IO_MIN_SIZE = 4096;

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
static uint32_t g_record_bytes = 1536;
static uint32_t g_large_block_ops_bytes = 131072; //128K
static FILE* g_output_file;
static device* g_device;

//==========================================================
// Forward Declarations
//
static void	set_scheduler();
static bool read_op(uint64_t p_off);
static bool write_op(uint64_t p_off, char* message);
static bool config_parse_device_name(char* device);
static bool	configure(int argc, char* argv[]);
static bool discover_num_blocks(device* p_device);
static int fd_get(device* p_device);
static uint64_t rand_48();
static uint64_t discover_min_op_bytes(int fd, const char *name);
static inline uint64_t random_read_offset(device* p_device);
static inline uint8_t* cf_valloc(size_t size);
static inline uint8_t* align_4096(uint8_t* stack_buffer);
static uint64_t read_from_device(device* p_device, uint64_t offset,
					uint32_t size, char* p_buffer);
static uint64_t write_to_device(device* p_device, uint64_t offset,
					uint32_t size, uint8_t* p_buffer);

//==========================================================
// Main
//
int main(int argc, char* argv[]) {
	printf("\n=> Raw Device Access - direct IO test\n");

	if (! configure(argc, argv)){
		exit(-1);
	}

	set_scheduler();
	srand(time(NULL));

	uint64_t offset = random_read_offset(g_device);
	fprintf(g_output_file, "\n-> Random read Offset: %" PRIu64 "\n\n", offset);

	int i;
	char* messages[] = {"DareDevil it's a great show!",
						"Flash it's a great show!",
						"Jessica Jones it's a great show!",
						"Californication it's a great show!",
						"Under the Dome it's a great show!"};
	for (i=0; i<5; i++){
		write_op(offset+(i*512), messages[i]);
	}
	fprintf(g_output_file, "\n");
	for (i=0; i<5; i++){
		read_op(offset+(4*512)-(i*512));
	}

	fprintf(g_output_file,"\n => Raw Device Access - output file closed.\n\n");
	fclose(g_output_file);
	free(g_device);
	return 0;
}

//==========================================================
// Read and Write functions
//

//------------------------------------------------
// Read Operation.
//
static bool read_op(uint64_t p_off){
	char* p_buffer = cf_valloc(g_device->read_bytes);

	if (p_buffer) {
		if (read_from_device(g_device, p_off, g_device->read_bytes, p_buffer)){
			free(p_buffer);
			return false;
		}
		fprintf(g_output_file, "- Reading: %s\nSize: %d\n", p_buffer, (int)strlen(p_buffer));
	}
	else {
		fprintf(stdout, "ERROR: read buffer cf_valloc()\n");
	}

	free(p_buffer);
	return true;
}


//------------------------------------------------
// Write Operation.
//
static bool write_op(uint64_t p_off, char* message){
	uint8_t* p_buffer = cf_valloc(g_device->read_bytes);

	if (p_buffer) {
		strcpy(p_buffer, message);
		if (write_to_device(g_device, p_off, g_device->read_bytes, p_buffer)){
			free(p_buffer);
			return false;
		}
		fprintf(g_output_file, "- Writing: %s\nSize: %d\n", p_buffer, (int)strlen(p_buffer));
	}
	else {
		fprintf(stdout, "ERROR: write buffer cf_valloc()\n");
	}

	free(p_buffer);
	return true;
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
	device* dev = malloc(sizeof(device));
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
		fprintf(g_output_file, "\n=> Raw Device Access - output file created\n\n");
		printf("-> Output file created. Access it when done.\n\n");
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

	if (! config_out_file(argv[2])){
		printf("=> ERROR: Couldn't create output file: %s\n", argv[1]);
		return false;
	}

	if (! config_parse_device_name(argv[1])){
		printf("=> ERROR: Couldn't parse device name.\n");
		return false;
	}

	fprintf(g_output_file,"-> Configuration was a success!\n");
	fprintf(g_output_file,"-> Device name: %s\n", g_device->name);

	if (! discover_num_blocks(g_device)){
		printf("=> ERROR: Couldn't discover number of blocks.\n");
		return false;
	}

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
static bool discover_num_blocks(device* p_device) {
	int fd = fd_get(p_device);

	if (fd == -1) {
		return false;
	}

	uint64_t device_bytes = 0;

	ioctl(fd, BLKGETSIZE64, &device_bytes);
	p_device->num_large_blocks = device_bytes / g_large_block_ops_bytes;
	p_device->min_op_bytes = discover_min_op_bytes(fd, p_device->name);
	//fd_put(p_device, fd);

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

	fprintf(g_output_file, "-> Blocks Infomation:\n - %s size = %" PRIu64 " bytes\n - %" PRIu64 " large blocks\n - "
		"%" PRIu64 " %" PRIu32 "-byte blocks\n - reads are %" PRIu32 " bytes\n",
			p_device->name, device_bytes, p_device->num_large_blocks,
			num_min_op_blocks, p_device->min_op_bytes, p_device->read_bytes);

	return true;
}


//------------------------------------------------
// Do one device read operation.
//
static uint64_t read_from_device(device* p_device, uint64_t offset,
		uint32_t size, char* p_buffer) {
	int fd = fd_get(p_device);

	if (fd == -1) {
		return -1;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			read(fd, p_buffer, size) != (ssize_t)size) {
		close(fd);
		fprintf(g_output_file, "ERROR: Couldn't seek & read\n");
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
		fprintf(g_output_file, "ERROR: Couldn't seek & write\n");
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
static void set_scheduler() {
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
		fprintf(g_output_file, "ERROR: couldn't open %s\n", scheduler_file_name);
	}

	else if (fwrite(mode, mode_length, 1, scheduler_file) != 1) {
		fprintf(g_output_file, "ERROR: writing %s to %s\n", mode,
			scheduler_file_name);
	}
	else{
		fclose(scheduler_file);
	}
}

//------------------------------------------------
// Discover device's minimum direct IO op size.
//
static uint64_t discover_min_op_bytes(int fd, const char *name) {
	off_t off = lseek(fd, 0, SEEK_SET);

	if (off != 0) {
		fprintf(g_output_file, "ERROR: %s seek\n", name);
		return 0;
	}

	uint8_t *buf = cf_valloc(HI_IO_MIN_SIZE);
	size_t read_sz = LO_IO_MIN_SIZE;

	while (read_sz <= HI_IO_MIN_SIZE) {
		if (read(fd, (void*)buf, read_sz) == (ssize_t)read_sz) {
			free(buf);
			return read_sz;
		}

		read_sz <<= 1; // LO_IO_MIN_SIZE and HI_IO_MIN_SIZE are powers of 2
	}

	fprintf(g_output_file, "ERROR: %s read failed at all sizes from %u to %u bytes\n",
			name, LO_IO_MIN_SIZE, HI_IO_MIN_SIZE);

	free(buf);

	return 0;
}

//------------------------------------------------
// Get a random 48-bit uint64_t.
//
static uint64_t rand_48() {
	return ((uint64_t)rand() << 16) | ((uint64_t)rand() & 0xffffULL);
}

//------------------------------------------------
// Get a random read offset for a device.
//
static inline uint64_t random_read_offset(device* p_device) {
	return (rand_48() % p_device->num_read_offsets) * p_device->min_op_bytes;
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