#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <linux/limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <istream>
#include <vector>
#include <string>
#include <iostream>
#include <assert.h>
#include "pf_utils.h"

using namespace std;
static int test_fd = 0;
static char test_file[PATH_MAX];

struct cmd{
	char op;
	uint64_t length;
	string src_file;
	uint64_t src_offset;
	uint64_t vol_offset;
};


static int do_append(cmd& c)
{
	//printf("append at:%ld from file:%s:%ld\n", aof->file_length(), c.src_file.c_str(), c.src_offset);
	int f = open(c.src_file.c_str(), O_RDONLY);
	if(f<0){
		fprintf(stderr, "Failed to open file:%s, (%d):%s", c.src_file.c_str(), errno, strerror(errno));
		return -errno;
	}
	DeferCall _c([f]() { close(f); });

	void* buf = malloc(c.length);
	if(buf == NULL){
		fprintf(stderr, "Failed alloc memory:%ld", c.length);
		return -ENOMEM;
	}
	DeferCall _c2([buf]() { free(buf); });
	ssize_t r = pread(f, buf, c.length, c.src_offset);
	if(r != c.length){
		fprintf(stderr, "Failed to read file:%s, %ld  rc:%d", c.src_file.c_str(), r, -errno);
		return -errno;
	}
	r  = write(test_fd, buf, c.length);
	if (r != c.length) {
		fprintf(stderr, "Failed to append aof, %ld  rc:%d",  r, -errno);
		return -errno;
	}
	return 0;
}
static int do_sync()
{
	int rc = fsync(test_fd);
	if(rc != 0)
		perror( "fsync error");
	return rc;
}
static int do_read(cmd& c)
{
	printf("write at:%ld to file:%s:%ld\n", c.src_offset,  c.src_file.c_str(), c.length);
	int f = open(c.src_file.c_str(), O_RDWR|O_CREAT, 0666);
	if (f < 0) {
		fprintf(stderr, "Failed to open file:%s, (%d):%s", c.src_file.c_str(), errno, strerror(errno));
		return -errno;
	}
	DeferCall _c([f]() { close(f); });

	void* buf = malloc(c.length);
	if (buf == NULL) {
		fprintf(stderr, "Failed alloc memory:%ld", c.length);
		return -ENOMEM;
	}
	DeferCall _c2([buf]() { free(buf); });
	ssize_t r = pread(test_fd, buf, c.length, c.vol_offset);
	if (r < 0) {
		fprintf(stderr, "Failed to append aof, %ld  rc:%d", r, -errno);
		return -errno;
	}
	//S5LOG_INFO("Read buf:%p first QWORD:0x%lx", buf, *(long*)buf);
	ssize_t r2 = pwrite(f, buf, r, c.src_offset);
	if (r2 != r) {
		fprintf(stderr, "Failed to read file:%s, %ld  rc:%d", c.src_file.c_str(), r, -errno);
		return -errno;
	}
	fsync(f);
	return 0;
}
static void                print_usage(void)
{ 
	fprintf(stdout, "vn_test_helper [options] file_name\n"
	                "options:\n"
					"   -a append only mode\n");
}
int main(int argc, char** argv)
{
	char buf[1024];
	int rc = 0;

	static struct option long_ops[] = {
		//{"file", required_argument, NULL, 'f'},
		{"help", no_argument, NULL, 'h'},
		{"append", no_argument, NULL, 'a'},
		{NULL, 0, NULL, 0},


	};

	int flags=0;

	while ((rc = getopt_long(argc, argv, "ah", long_ops, NULL)) != -1) {
		switch (rc) {
		//case 'f':
		//	strncpy(file_name, optarg, PATH_MAX - 1);
		//	break;
		case 'h':
			print_usage();
			return 0;
		case 'a':
			flags |= O_APPEND;
			fprintf(stderr, "open in append only mode\n");
			break;
		case '?':
			fprintf(stderr, "unknown option %c\n", rc);
			print_usage();
			return -1;
		default:
			;
		}
	}
	if (optind >= argc) {
		fprintf(stderr, "File name not specified\n");
		print_usage();
		return -1;
	}

	strncpy(test_file, argv[optind], PATH_MAX - 1);

	fprintf(stderr, "use test file:%s\n", test_file);
	test_fd = open(test_file, O_CREAT | O_RDWR | flags, 00666);
	if(test_fd < 0) {
		fprintf(stderr, "Failed to open file:%s\n", test_file);
		return EINVAL;
	}
	rc=0;
	while (rc == 0) {
		fprintf(stderr, "%d >", rc);
		cin.getline(buf, sizeof(buf));
		if((cin.rdstate() & std::ios_base::eofbit) != 0) {
			fprintf(stderr, "stdin EOF \n");
			return 0;
		}
		fprintf(stderr, "get line:%s\n", buf);
		vector<string> args = split_string(buf, ' ');
		if (args.size() < 1)
			continue;
		if (args[0].size() > 1) {
			fprintf(stderr, "Invalid cmd line:%s", buf);
			return EINVAL;
		}
		cmd c;
		c.op = args[0][0];
		switch (c.op) {
		case 'a':
			assert(args.size() == 4);
			c.length = strtol(args[1].c_str(), NULL, 10);
			assert(errno == 0);
			c.src_file = args[2];
			c.src_offset = strtol(args[3].c_str(), NULL, 10);
			assert(errno == 0);
			rc = do_append(c);
			break;
		case 's':
			assert(args.size() == 1);
			rc = do_sync();
			break;
		case 'r':
			assert(args.size() == 5);
			c.length = strtol(args[1].c_str(), NULL, 10);
			assert(errno == 0);
			c.vol_offset = strtol(args[2].c_str(), NULL, 10);
			assert(errno == 0);
			c.src_file = args[3];
			c.src_offset = strtol(args[4].c_str(), NULL, 10);
			assert(errno == 0);
			rc = do_read(c);
			break;
		case 'w':
			assert(args.size()>1);
			assert(write(test_fd, &buf[2], strlen(&buf[2])) > 0);
			break;
		case 'q':
			exit(0);
		default:
			fprintf(stderr, "Invalid cmd line:%s, len:%ld\n", buf, strlen(buf));
			exit(1);
		}
	}
	return rc;
}
