/*
	S1Search Research 
	Raw Device Access: Reading and Writing directly on a SSD
*/

//==========================================================
// Includes
//
#include <inttypes.h>
#include <fcntl.h>
#include <pthread.h>
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

#include "clock.h"

//==========================================================
// Constants
//
#define THREADS_MAX_NUM 200
#define LATENCY_MAX_NUM 6000

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
volatile int g_running_threads = 0;
static int g_read_threads = 0;
static int g_write_threads = 0;
static long g_reads_counter = 0;
static long g_writes_counter = 0;
static uint32_t g_number_of_threads = 0;
static uint64_t g_ops_times[LATENCY_MAX_NUM][2];
static uint64_t g_total_time_threads = 0;
static uint64_t g_read_reqs_per_sec = 0;
static uint64_t g_run_us = 0;
static bool g_running;
static uint64_t g_run_start_us;
static pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;

static int g_fd_device = -1;
static int g_ref_tab_columns = 4; //Division of SSD sector //------NOVO--------//
static char g_device_name[MAX_DEVICE_NAME_SIZE];
static uint32_t g_scheduler_mode = 0; //noop mode
static uint32_t g_record_bytes = 512; 
static uint32_t g_large_block_ops_bytes = 131072; //128K
static bool *g_ref_tab = NULL;

static FILE* g_output_file;
static device* g_device;

//==========================================================
// Forward Declarations
//
static void print_final_info();
static void *read_op(void *thread_num);
static void *write_op(void *thread_num);
static void array_add(uint64_t value);
static void stressTest(int argc, char* argv[]);
static void percentile_array(uint64_t array[][2]);
static uint64_t rand_48();
static inline uint64_t safe_delta_ns(uint64_t start_ns, uint64_t stop_ns);
static inline uint64_t random_read_offset(device* p_device);
static inline uint8_t* align_4096(uint8_t* stack_buffer);
static bool thread_creation_op(int num_of_threads);
static bool config_is_arg_alpha(char *argv);
static bool configure_stress_test(int argc, char* argv[]);

static char* mystrncpy(char *s1, const char *s2, size_t n, char c);
static int fd_get(device* p_device);
static inline uint8_t* cf_valloc(size_t size);
static void	set_scheduler();
static void print_ref_tab(); //------NOVO--------//
static void erase_sector_ref(uint64_t sector, uint32_t div); //------NOVO--------//
static void add_sector_ref(uint64_t sector, uint32_t div); //------NOVO--------//
static void prep_to_sector_div(uint64_t offset, uint32_t division, void* dest, char* message); //------NOVO--------//
static bool show_sector_ref(uint64_t offset, uint32_t division);
static uint64_t discover_min_op_bytes(int fd, const char *name);
static bool is_sector_free(uint64_t sector, uint32_t div); //------NOVO--------//
static bool	configure(int argc, char* argv[]);
static bool config_is_arg_num(char *argv);
static bool config_parse_device_name(char* device);
static bool discover_num_blocks(device* p_device);
static bool read_from_device(device* p_device, uint64_t offset,
					uint32_t size, void* p_buffer);
static bool write_to_device(device* p_device, uint64_t offset,
					uint32_t size, void* p_buffer);

char* readJNA(char* device_name, uint32_t size, uint64_t offset, uint32_t division);
bool writeJNA(char* device_name, char* message, uint64_t offset, uint32_t division);

//======================================================================================================
// Main
//
int main(int argc, char* argv[]) {
	//stressTest(argc, argv);
	//586072319

	char *result;
	char *inital = "Some Initial String. Some Initial String. Some Initial String. Some Initial String. Some Initial String. Some Initial String. Some Initial String. Some Initial String. Some Initial String. Some Initial String. Some Initial String. Some Initial String. Some";

	writeJNA(argv[1], inital, atol(argv[3]), 0);
	writeJNA(argv[1], argv[4], atol(argv[3]), 1);
	result = readJNA(argv[1], atoi(argv[2]), atol(argv[3]), 1);
	result = readJNA(argv[1], atoi(argv[2]), atol(argv[3]), 0);

	free(g_ref_tab);
	return 0;
}

//======================================================================================================
// Stress Test Function
//
static void stressTest(int argc, char* argv[]){
	printf("\n=> Raw Device Access - direct IO Stress test Begins\n");

	if (! configure_stress_test(argc, argv)){
		exit(-1);
	}

	if (! configure(argc, argv)){
		exit(-1);
	}

	set_scheduler();
	srand(time(NULL));

	g_running = true;

    if (! thread_creation_op(g_number_of_threads)){
    	exit(-1);
    }

	uint64_t now_us = 0, main_start_time = cf_getus(), main_total_time;
	uint64_t run_stop_us = main_start_time + g_run_us;

	printf("\n-> Init of test\n");
	while (g_running && (now_us = cf_getus()) < run_stop_us){
		printf(" - %" PRIu64 " seconds remaining              \r", (run_stop_us - now_us)/1000000);
		fflush(stdout);
		sleep(1);
	}

	g_running = false;
	printf("\n-> Test Finished!\n");
	main_total_time = cf_getus() - main_start_time;

	while (g_running_threads > 0){
		sleep(1);
		printf(" - Waiting Threads finish. Remaining Threads: %d          \r", g_running_threads);
		fflush(stdout);
	}

	printf("\n-> Done!\n");
	print_final_info(main_total_time);
	print_ref_tab();
	printf("\n=> Raw Device Access - direct IO Stress test Ends\n");
	
	fclose(g_output_file);
	free(g_ref_tab);
	free(g_device);
}

//======================================================================================================
// Functions for JNA/JNI use
//

//------------------------------------------------
// Read sectors function for JNA
//
char* readJNA(char* device_name, uint32_t size, uint64_t offset, uint32_t division){
	if (! config_parse_device_name(device_name)){
    	printf("=> ERROR: Couldn't parse device name: %s\n", device_name);
		exit(-1);
    }else
    {g_record_bytes = size;}

    if (! discover_num_blocks(g_device)){
		printf("=> ERROR: Couldn't discover number of blocks.\n");
		exit(-1);
	}

	set_scheduler();
		
	int sector_div = g_device->read_bytes/g_ref_tab_columns;
	char *message = cf_valloc(sector_div);
	void *p_buffer = cf_valloc(g_device->read_bytes);

	if (! p_buffer) {
		printf("=> ERROR: read buffer cf_valloc()\n");
		return NULL;
	}

	offset = (offset % g_device->num_read_offsets) * g_device->min_op_bytes;

	if(! is_sector_free(offset/g_device->read_bytes, division)){
		if (! read_from_device(g_device, offset, g_device->read_bytes, p_buffer)){
				printf("=> ERROR read op on offset: %" PRIu64 "\n", offset);
				free(p_buffer);
				exit(-1);
		}else{
			strncpy(message, p_buffer+(sector_div*division), sector_div);
			//mystrncpy(message, p_buffer+(sector_div*division), sector_div, '0');
			printf("Message = %s\n", message);
		}
	}else{
		printf("=> Sector NOT referenced!\n");
	}

	free(p_buffer);
	free(g_device);
	free(message);
	return message;
}

//------------------------------------------------
// Write sectors function for JNA 
//
bool writeJNA(char* device_name, char* message, uint64_t offset, uint32_t division){
	if (! config_parse_device_name(device_name)){
		printf("=> ERROR: Couldn't parse device name: %s\n", device_name);
		exit(-1);
	}else
	{g_record_bytes = (uint32_t)strlen(message);}
	
	if (! discover_num_blocks(g_device)){
		printf("=> ERROR: Couldn't discover number of blocks.\n");
		exit(-1);
	}
	
	set_scheduler();

	void *p_buffer = cf_valloc(g_device->read_bytes);

	if (! p_buffer) {
		printf("=> ERROR: read buffer cf_valloc()\n");
		return false;
	}

	offset = (offset % g_device->num_read_offsets) * g_device->min_op_bytes;

	if(is_sector_free(offset/g_device->read_bytes, division)){
		prep_to_sector_div(offset, division , p_buffer, message);
		if (! write_to_device(g_device, offset, g_device->read_bytes, p_buffer)){
				printf("=> ERROR write op on offset: %" PRIu64 "\n", offset);
				free(p_buffer);
				exit(-1);
		}else{
			add_sector_ref(offset/g_device->read_bytes, division);
		}
	}else{
		printf("=> Sector ALREADY referenced!\n");
	}
	
	free(p_buffer);
	free(g_device);
	return true;
}

static char* mystrncpy(char *s1, const char *s2, size_t n, char c){

	char *s = s1;
	while (n > 0 && *s2 != '\0') {
		*s++ = *s2++;
		--n;
    }
    while (n > 0) {
    	if (n > 2)
    	{*s++ = c;}
    	else
		{*s++ = '\0';}
		--n;
    }
}

//======================================================================================================
// Threads
//

//------------------------------------------------
// Thread Creation Operation.
//
static bool thread_creation_op(int num_of_threads){
	int i;
	pthread_t threads[num_of_threads];

	if (num_of_threads < 0 || num_of_threads > THREADS_MAX_NUM){
		printf("=> ERROR: invalid number of threads!\n");
		return false;
	}
	
	// for (i=0; i < (num_of_threads); i++){
	// 	int *thread_num = malloc(sizeof(int *));
	// 	*thread_num = (int)g_running_threads;

	// 	//READ ONLY
	// 	pthread_create(&(threads[i]), NULL, read_op, thread_num);

	// 	pthread_mutex_lock(&running_mutex);
	// 	g_read_threads++;
 //    	g_running_threads++;
 //    	pthread_mutex_unlock(&running_mutex);

 //    	//WRITE ONLY
 //    	pthread_create(&(threads[i]), NULL, write_op, thread_num);

	// 	pthread_mutex_lock(&running_mutex);
	// 	g_write_threads++;
 //    	g_running_threads++;
 //    	pthread_mutex_unlock(&running_mutex);
	// }
	
	// READ AND WRITE
	for (i=0; i < (num_of_threads*3)/4; i++){
		int *thread_num = malloc(sizeof(int *));
		*thread_num = (int)g_running_threads;
		pthread_create(&(threads[i]), NULL, read_op, thread_num);

		pthread_mutex_lock(&running_mutex);
		g_read_threads++;
    	g_running_threads++;
    	pthread_mutex_unlock(&running_mutex);

	}
	for (i= (num_of_threads*3)/4; i < num_of_threads; i++){
		int *thread_num = malloc(sizeof(int *));
		*thread_num = (int)g_running_threads;
		pthread_create(&(threads[i]), NULL, write_op, thread_num);

		pthread_mutex_lock(&running_mutex);
		g_write_threads++;
    	g_running_threads++;
    	pthread_mutex_unlock(&running_mutex);
	}

	return true;
}

//------------------------------------------------
// Thread Read Operation.
//
static void *read_op(void *thread_num){
	int counter = 0;	
	uint64_t begin_op_time, offset;
	void* p_buffer = cf_valloc(g_device->read_bytes);

	if (! p_buffer) {
		printf("=> ERROR: read buffer cf_valloc()\n");
		return NULL;
	}

	while (g_running){
		offset = random_read_offset(g_device);
		begin_op_time = cf_getus();
		if (! read_from_device(g_device, offset, g_device->read_bytes, p_buffer)){
			printf("=> ERROR read op number: %d; Offset: %" PRIu64 "\n", counter+1, offset);
		}
		else{
			array_add(safe_delta_ns(begin_op_time, cf_getus())/10);
			counter++;
		}
	}

	pthread_mutex_lock(&running_mutex);
	g_reads_counter += counter;
	g_running_threads--;
	pthread_mutex_unlock(&running_mutex);

	

   	free(p_buffer);
   	free(thread_num);
   	return NULL;
}

//------------------------------------------------
// Thread Write Operation.
//
static void *write_op(void *thread_num){
	int counter = 0;
	uint64_t begin_op_time, offset;
	char* message = "Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Hello SSD.Regards, thread";
	void* p_buffer = cf_valloc(g_device->read_bytes);

	if (! p_buffer) {
		printf("=> ERROR: write buffer cf_valloc()\n");
		return NULL;
	}

	//strcpy(p_buffer, message);

	while (g_running){
		offset = random_read_offset(g_device);
		int division = rand()%g_ref_tab_columns;
		if (is_sector_free(offset/g_device->read_bytes, division)){
			prep_to_sector_div(offset, division , p_buffer, message);
			begin_op_time = cf_getus();
			if (! write_to_device(g_device, offset, g_device->read_bytes, p_buffer)){
				printf("=> ERROR write op number: %d; Offset: %" PRIu64 "\n", counter+1, offset);
			}
			else{
				array_add(safe_delta_ns(begin_op_time, cf_getus())/10);
				add_sector_ref(offset/g_device->read_bytes, division);
				counter++;
			}
		}
	}

	pthread_mutex_lock(&running_mutex);
	g_writes_counter += counter;
	g_running_threads--;
	pthread_mutex_unlock(&running_mutex);

   	free(p_buffer);
   	free(thread_num);
   	return NULL;
}

//======================================================================================================
// Helpers
//

//------------------------------------------------
// Print information from the test
//
static void print_final_info(uint64_t main_total_time){
	fprintf(g_output_file, "__________________________________________\n");
	fprintf(g_output_file, "Total time: %" PRIu64 " s\n", main_total_time/1000000);
	fprintf(g_output_file, "Total threads created: %d\n", g_read_threads + g_write_threads);
	fprintf(g_output_file, "Number of reads threads: %" PRIu64 "\n", g_read_threads);
	fprintf(g_output_file, "Number of reads counter: %" PRIu64 "\n", g_reads_counter);
	if(g_read_threads != 0)
	{fprintf(g_output_file, "Average reads operations counter: %" PRIu64 "\n", g_reads_counter/g_read_threads);}
	fprintf(g_output_file, "Number of writes threads: %" PRIu64 "\n", g_write_threads);
	fprintf(g_output_file, "Number of writes counter: %" PRIu64 "\n", g_writes_counter);
	if(g_write_threads != 0)
	{fprintf(g_output_file, "Average writes operations counter: %" PRIu64 "\n", g_writes_counter/g_write_threads);}
	fprintf(g_output_file, "Reads per second: %" PRIu64 "\n", g_reads_counter/(main_total_time/1000000));
	fprintf(g_output_file, "Writes per second: %" PRIu64 "\n", g_writes_counter/(main_total_time/1000000));
		
	fprintf(g_output_file, "\n");
	percentile_array(g_ops_times);
	fprintf(g_output_file,"\n => Raw Device Access - output file closed.\n");
	fprintf(g_output_file, "===========================================\n");
}

//------------------------------------------------
// Parse if argument has alphas
//
static bool config_is_arg_alpha(char *argv){
	char *arg = argv;
	while (*arg != 0){
	    if( isalpha(*arg) ){
	    	arg++; 
	    }
	    else{
	    	return false;
	    }        
	}
	return true;
}

//------------------------------------------------
// Parse if argument has a number
//
static bool config_is_arg_num(char *argv){
	char *arg = argv;
	while (*arg != 0){
	    if( isdigit(*arg) ){
	    	arg++; 
	    }
	    else{
	    	return false;
	    }        
	}
	return true;
}

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
		fprintf(g_output_file, "===========================================\n");
		fprintf(g_output_file, "=> Raw Device Access - output file created\n\n");
		printf("-> Output file created. Access it when done.\n");
	}
	return true;
}

//------------------------------------------------
// Set run parameters.
//
static bool	configure(int argc, char* argv[]){
	if (argc <= 3){
		printf("=> ERROR: Wrong number of arguments(%d)!\nUsage: ./raw device buffer(size)\nExample: ./raw /dev/sdc 512\n",argc);
		return false;
	}

	if (! config_parse_device_name(argv[1])){
		printf("=> ERROR: Couldn't parse device name: %s\n", argv[1]);
		return false;
	}

	if (! config_is_arg_num(argv[2])){
		printf("=> ERROR: Argument is not a valid number: %s\n", argv[2]);
		return false;
	}
	else
	{g_record_bytes = (uint32_t) atoi(argv[2]);}

	if (! discover_num_blocks(g_device)){
		printf("=> ERROR: Couldn't discover number of blocks.\n");
		return false;
	}

	set_scheduler(); 
	return true;
}

static bool configure_stress_test(int argc, char* argv[]){
	if (argc != 6){
		printf("=> ERROR: Wrong number of arguments!\nUsage: ./raw device buffer(size) threads(num) time(seconds) resultfile\nExample: ./raw /dev/sdc 512 50 60 output.txt");
		return false;
	}

	if (! config_out_file(argv[5])){
		printf("=> ERROR: Couldn't create output file: %s\n", argv[4]);
		return false;
	}

	if (! config_is_arg_num(argv[4]) && ! config_is_arg_num(argv[3])){
		printf("=> ERROR: Argument is not a valid number: %s or %s\n", argv[3], argv[4]);
		return false;
	}
	else{
		g_run_us = (uint64_t) atoi(argv[4]) * 1000000;
		g_number_of_threads = (uint32_t) atoi(argv[3]);
	}

	memset(g_ops_times, 0, sizeof(g_ops_times));
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
		printf("=> ERROR: Couldn't open device %s\n", p_device->name);
	}

	return fd;
}
	
//------------------------------------------------
// Do one device read operation.
//
static bool read_from_device(device* p_device,uint64_t offset,uint32_t size, void* p_buffer) {
	int fd = g_fd_device; //fd_get(p_device);

	if (fd == -1) {
		return false;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			read(fd, p_buffer, size) != (ssize_t)size) {
		close(fd);
		printf("=> ERROR: Couldn't seek & read\n");
		return false;
	}
	
	uint64_t stop_ns = cf_getns();
	return true;
}

//------------------------------------------------
// Do one device write operation.
//
static bool write_to_device(device* p_device, uint64_t offset, uint32_t size, void* p_buffer) {
	int fd = g_fd_device; //fd_get(p_device);

	if (fd == -1) {
		return false;
	}

	if (lseek(fd, offset, SEEK_SET) != offset ||
			write(fd, p_buffer, size) != (ssize_t)size) {
		close(fd);
		printf("=> ERROR: Couldn't seek & write\n");
		return false;
	}

	uint64_t stop_ns = cf_getns();
	return true;
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
		printf("=> ERROR: couldn't open %s\n", scheduler_file_name);
	}

	else if (fwrite(mode, mode_length, 1, scheduler_file) != 1) {
		printf("=> ERROR: writing %s to %s\n", mode,
			scheduler_file_name);
	}
	else{
		fclose(scheduler_file);
	}
}

//------------------------------------------------
// Discover device storage capacity.
//
static bool discover_num_blocks(device* p_device) {
	int fd = fd_get(p_device);

	if (fd == -1) {
		return false;
	}

	g_fd_device = fd;
	uint64_t device_bytes = 0;

	ioctl(fd, BLKGETSIZE64, &device_bytes);
	p_device->num_large_blocks = device_bytes / g_large_block_ops_bytes;
	p_device->min_op_bytes = discover_min_op_bytes(fd, p_device->name);

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

	if (g_output_file){
		fprintf(g_output_file, "-> Blocks Infomation:\n - %s size = %" PRIu64 " bytes\n - %" PRIu64 " large blocks\n - "
			"%" PRIu64 " %" PRIu32 "-byte blocks\n - buffers are %" PRIu32 " bytes\n",
				p_device->name, device_bytes, p_device->num_large_blocks,
				num_min_op_blocks, p_device->min_op_bytes, p_device->read_bytes);
		fprintf(g_output_file, "__________________________________________\n");
	}

	if (!g_ref_tab)
	{
		g_ref_tab = malloc(sizeof(bool)* g_device->num_read_offsets * g_ref_tab_columns);
		memset(g_ref_tab, 0, sizeof(g_ref_tab));
		printf("Table of Reference created!\n");
	}

	return true;
}

//------------------------------------------------
// Discover device's minimum direct IO op size.
//
static uint64_t discover_min_op_bytes(int fd, const char *name) {
	off_t off = lseek(fd, 0, SEEK_SET);

	if (off != 0) {
		printf("=> ERROR: %s seek\n", name);
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

	printf("=> ERROR: %s read failed at all sizes from %u to %u bytes\n",
			name, LO_IO_MIN_SIZE, HI_IO_MIN_SIZE);

	free(buf);
	return 0;
}

//------------------------------------------------
// 
//
static void prep_to_sector_div(uint64_t offset, uint32_t division, void* dest, char* message){
	
	if (! read_from_device(g_device, offset, g_device->read_bytes, dest)){
		printf("=> ERROR read op ON ADD_TO_SECTOR. Offset: %" PRIu64 "\n", offset);
		return;
	}
	else{
		int sector_div = g_device->read_bytes/g_ref_tab_columns;
		memset(dest+(sector_div*division), '\0', sector_div);
		if (strlen(message) < sector_div){
			strncpy(dest+(sector_div*division), message, strlen(message));
		}else
		{strncpy(dest+(sector_div*division), message, sector_div - 1);}
	}
}

static bool show_sector_ref(uint64_t sector, uint32_t division){
	return *(g_ref_tab + (sector * g_ref_tab_columns + division));
}

//------------------------------------------------
// Check if sector is free
//
static bool is_sector_free(uint64_t sector, uint32_t division){
	if (division < g_ref_tab_columns && division >= 0){
		if (*(g_ref_tab + (sector * g_ref_tab_columns + division)) == 0){
			return true;
		}
	}
	return false;	
}

//------------------------------------------------
// Set sector on reference table
//
static void add_sector_ref(uint64_t sector, uint32_t division){
	if (division < g_ref_tab_columns && division >= 0){
		*(g_ref_tab + (sector * g_ref_tab_columns + division)) = 1;
	}
}

//------------------------------------------------
// Clear sector on reference table
//
static void erase_sector_ref(uint64_t sector, uint32_t division){
	if (division < g_ref_tab_columns && division >= 0){
		*(g_ref_tab + (sector * g_ref_tab_columns + division)) = 0;
	}
}

//------------------------------------------------
// Print reference table
//
static void print_ref_tab(){
	int i=0, j=0;
	FILE* out = fopen("ref_tab.txt", "w");

	if (! out) {
		return;
	}

	for (i=0; i< (g_device->num_read_offsets); i++)
	{
		for (j=0; j<g_ref_tab_columns; j++){
			if (!is_sector_free(i,j))
			{fprintf(out,"[%.*d->%1d]=%1d\n", 9, i, j, *(g_ref_tab+(i*g_ref_tab_columns+j)));}
		}
	}
}

//------------------------------------------------
// Add to a array
//
static void array_add(uint64_t value){
	int i =0;
	while (g_ops_times[i][1] != 0){
		if (g_ops_times[i][0] == value){
			pthread_mutex_lock(&running_mutex);
			g_ops_times[i][1]++;
			pthread_mutex_unlock(&running_mutex);
			return;
		}
		i++;
		if (i>=LATENCY_MAX_NUM){
			printf("Array g_ops_times passed across max num.\n");
			exit(-1);
		}
	}
	pthread_mutex_lock(&running_mutex);
	g_ops_times[i][0] = value; 
	g_ops_times[i][1] = 1;
	pthread_mutex_unlock(&running_mutex);
}
//------------------------------------------------
// Bubble sort array
//
static void percentile_array(uint64_t array[][2]){
	int i=0, j=0, max;

	while (g_ops_times[i][1] != 0){i++;}
	max = i;

	for (i=0; i<max; i++){
		for(j=0; j<max-1-i; j++){
			if (array[j][0] > array[j+1][0])
            {
                uint64_t aux =  array[j][0];
                array[j][0] = array[j+1][0];
                array[j+1][0] = aux;

                aux =  array[j][1];
                array[j][1] = array[j+1][1];
                array[j+1][1] = aux;
            }
		}
	}

	max = g_reads_counter + g_writes_counter;
	int counter = 0, perc50 = 50*(max+1)/100, perc90 = 90*(max+1)/100;

	for (i=0; i < LATENCY_MAX_NUM; i++){
		counter += g_ops_times[i][1];
		if (counter >= perc50 || g_ops_times[i+1][1] == 0){
			fprintf(g_output_file, "50th percentile: %.2f ms\n", ((float)g_ops_times[i][0])/100);
			perc50 = perc90*2;
		}
		if (counter >= perc90 || g_ops_times[i+1][1] == 0){
			fprintf(g_output_file, "90th percentile: %.2f ms\n", ((float)g_ops_times[i][0])/100);
			
			// i=0;
			// while (g_ops_times[i][1] != 0){
			// 	fprintf(g_output_file, "[%"PRIu64"]= %"PRIu64" ", g_ops_times[i][1], g_ops_times[i][0]);
			// 	i++;
			// }
			// fprintf(g_output_file, "\n");

			return;
		}	
	}	
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