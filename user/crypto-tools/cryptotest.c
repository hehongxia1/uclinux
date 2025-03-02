/*-
 * Copyright (c) 2004 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD: src/tools/tools/crypto/cryptotest.c,v 1.6 2004/09/07 18:35:00 sam Exp $
 */

/*
 * Simple tool for testing hardware/system crypto support.
 *
 * cryptotest [-czsbv] [-a algorithm] [count] [size ...]
 *
 * Run count iterations of a crypt+decrypt or mac operation on a buffer of
 * size bytes.  A random key and iv are used.  Options:
 *	-c	check the results
 *	-z	run all available algorithms on a variety of buffer sizes
 *	-v	be verbose
 *	-b	mark operations for batching
 *	-p	profile kernel crypto operations (must be root)
 *	-t n	fork n threads and run tests concurrently
 * Known algorithms are:
 *	null	null cbc
 *	des	des cbc
 *	3des	3des cbc
 *	blf	blowfish cbc
 *	cast	cast cbc
 *	skj	skipjack cbc
 *	aes	rijndael/aes 128-bit cbc
 *	aes192	rijndael/aes 192-bit cbc
 *	aes256	rijndael/aes 256-bit cbc
 *	md5	md5 hmac
 *	sha1	sha1 hmac
 *	sha256	256-bit sha2 hmac
 *	sha384	384-bit sha2 hmac
 *	sha512	512--bit sha2 hmac
 *
 * For a test of how fast a crypto card is, use something like:
 *	cryptotest -z 1024
 * This will run a series of tests using the available crypto/cipher
 * algorithms over a variety of buffer sizes.  The 1024 says to do 1024
 * iterations.  Extra arguments can be used to specify one or more buffer
 * sizes to use in doing tests.
 *
 * To fork multiple processes all doing the same work, specify -t X on the
 * command line to get X "threads" running simultaneously.  No effort is made
 * to synchronize the threads or otherwise maximize load.
 *
 * If the kernel crypto code is built with CRYPTO_TIMING and you run as root,
 * then you can specify the -p option to get a "profile" of the time spent
 * processing crypto operations.  At present this data is only meaningful for
 * symmetric operations.  To get meaningful numbers you must run on an idle
 * machine.
 *
 * Expect ~400 Mb/s for a Broadcom 582x for 8K buffers on a reasonable CPU
 * (64-bit PCI helps).  Hifn 7811 parts top out at ~110 Mb/s.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <paths.h>
#include <stdlib.h>

#include <sys/sysctl.h>
#include <sys/time.h>
#include <crypto/cryptodev.h>

#define	CHUNK	64	/* how much to display */
#define	N(a)		(sizeof (a) / sizeof (a[0]))
#define	streq(a,b)	(strcasecmp(a,b) == 0)

void	hexdump(char *, int);

int	verbose = 0;
int	opflags = 0;
int	verify = 0;

int swap_iv = 0;
int swap_key = 0;
int swap_data = 0;

static void swap_buf(char *buf, int len)
{
	int i;
	char tmp[4];
	for (i = 0; i < len; i += 4) {
		memcpy(tmp, &buf[i], 4);
		buf[i + 0] = tmp[3];
		buf[i + 1] = tmp[2];
		buf[i + 2] = tmp[1];
		buf[i + 3] = tmp[0];
	}
}

#define MAX_IV_SIZE 16

struct alg {
	const char* name;
	int	ishash;
	int	blocksize;
	int	minkeylen;
	int	maxkeylen;
	int	code;
} algorithms[] = {
#ifdef CRYPTO_NULL_CBC
	{ "null",	0,	8,	1,	256,	CRYPTO_NULL_CBC },
#endif
	{ "des",	0,	8,	8,	8,	CRYPTO_DES_CBC },
	{ "3des",	0,	8,	24,	24,	CRYPTO_3DES_CBC },
	{ "blf",	0,	8,	5,	56,	CRYPTO_BLF_CBC },
	{ "cast",	0,	8,	5,	16,	CRYPTO_CAST_CBC },
	{ "skj",	0,	8,	10,	10,	CRYPTO_SKIPJACK_CBC },
	{ "aes",	0,	16,	16,	16,	CRYPTO_RIJNDAEL128_CBC},
	{ "aes192",	0,	16,	24,	24,	CRYPTO_RIJNDAEL128_CBC},
	{ "aes256",	0,	16,	32,	32,	CRYPTO_RIJNDAEL128_CBC},
#ifdef notdef
	{ "arc4",	0,	8,	1,	32,	CRYPTO_ARC4 },
#endif
	{ "md5",	1,	8,	16,	16,	CRYPTO_MD5 },
	{ "md5_hmac",	1,	8,	16,	16,	CRYPTO_MD5_HMAC },
	{ "sha1",	1,	8,	20,	20,	CRYPTO_SHA1 },
	{ "sha1_hmac",	1,	8,	20,	20,	CRYPTO_SHA1_HMAC },
	{ "sha256",	1,	8,	32,	32,	CRYPTO_SHA2_HMAC },
	{ "sha384",	1,	8,	48,	48,	CRYPTO_SHA2_HMAC },
	{ "sha512",	1,	8,	64,	64,	CRYPTO_SHA2_HMAC },
};

static void
usage(const char* cmd)
{
	printf("usage: %s [-c] [-z] [-s] [-b] [-v] [-a algorithm] [count] [size ...]\n",
		cmd);
	printf("where algorithm is one of:\n");
	printf("    des 3des (default) blowfish cast skipjack\n");
	printf("    aes (aka rijndael) aes192 aes256 arc4\n");
	printf("count is the number of encrypt/decrypt ops to do\n");
	printf("size is the number of bytes of text to encrypt+decrypt\n");
	printf("\n");
	printf("-c check the results (slows timing)\n");
	printf("-z run all available algorithms on a variety of sizes\n");
	printf("-v be verbose\n");
	printf("-b mark operations for batching\n");
	printf("-p profile kernel crypto operation (must be root)\n");
	exit(-1);
}

static struct alg*
getalgbycode(int cipher)
{
	int i;

	for (i = 0; i < N(algorithms); i++)
		if (cipher == algorithms[i].code)
			return &algorithms[i];
	return NULL;
}

static struct alg*
getalgbyname(const char* name)
{
	int i;

	for (i = 0; i < N(algorithms); i++)
		if (streq(name, algorithms[i].name))
			return &algorithms[i];
	return NULL;
}

static int
devcrypto(void)
{
	static int fd = -1;

	if (fd < 0) {
		fd = open(_PATH_DEV "crypto", O_RDWR, 0);
		if (fd < 0)
			err(1, _PATH_DEV "crypto");
		if (fcntl(fd, F_SETFD, 1) == -1)
			err(1, "fcntl(F_SETFD) (devcrypto)");
	}
	return fd;
}

static int
crget(void)
{
	int fd;

	if (ioctl(devcrypto(), CRIOGET, &fd) == -1)
		err(1, "ioctl(CRIOGET)");
	if (fcntl(fd, F_SETFD, 1) == -1)
		err(1, "fcntl(F_SETFD) (crget)");
	return fd;
}

static char
rdigit(void)
{
#if 1
	const char a[] = {
		0x10,0x54,0x11,0x48,0x45,0x12,0x4f,0x13,0x49,0x53,0x14,0x41,
		0x15,0x16,0x4e,0x55,0x54,0x17,0x18,0x4a,0x4f,0x42,0x19,0x01
	};
	return 0x20+a[random()%N(a)];
#else
	static unsigned char c = 0;
	return (c++);
#endif
}

static void
runtest(struct alg *alg, int count, int size, int cmd, struct timeval *tv)
{
	int i, fd = crget();
	struct timeval start, stop, dt;
	char *cleartext, *ciphertext, *originaltext;
	struct session_op sop;
	struct crypt_op cop;
	char iv[MAX_IV_SIZE];

	bzero(&sop, sizeof(sop));
	if (!alg->ishash) {
		sop.keylen = (alg->minkeylen + alg->maxkeylen)/2;
		sop.key = (char *) malloc(sop.keylen);
		if (sop.key == NULL)
			err(1, "malloc (key)");
		for (i = 0; i < sop.keylen; i++)
			sop.key[i] = rdigit();
		sop.cipher = alg->code;
		if (swap_key)
			swap_buf(sop.key, sop.keylen);
	} else {
		sop.mackeylen = (alg->minkeylen + alg->maxkeylen)/2;
		sop.mackey = (char *) malloc(sop.mackeylen);
		if (sop.mackey == NULL)
			err(1, "malloc (mac)");
		for (i = 0; i < sop.mackeylen; i++)
			sop.mackey[i] = rdigit();
		sop.mac = alg->code;
		if (swap_key)
			swap_buf(sop.mackey, sop.mackeylen);
	}
	if (ioctl(fd, cmd, &sop) < 0) {
		if (cmd == CIOCGSESSION) {
			close(fd);
			if (verbose) {
				printf("cipher %s", alg->name);
				if (alg->ishash)
					printf(" mackeylen %u\n", sop.mackeylen);
				else
					printf(" keylen %u\n", sop.keylen);
				perror("CIOCGSESSION");
			}
			/* hardware doesn't support algorithm; skip it */
			return;
		}
		printf("cipher %s keylen %u mackeylen %u\n",
			alg->name, sop.keylen, sop.mackeylen);
		err(1, "CIOCGSESSION");
	}

	originaltext = malloc(3*size);
	if (originaltext == NULL)
		err(1, "malloc (text)");
	cleartext = originaltext+size;
	ciphertext = cleartext+size;
	for (i = 0; i < size; i++)
		cleartext[i] = rdigit();
	memcpy(originaltext, cleartext, size);
	for (i = 0; i < N(iv); i++)
		iv[i] = rdigit();

	if (verbose) {
		printf("session = 0x%x\n", sop.ses);
		printf("count = %d, size = %d\n", count, size);
		if (!alg->ishash) {
			printf("iv:");
			hexdump(iv, sizeof iv);
		}
		printf("cleartext:");
		hexdump(cleartext, MIN(size, CHUNK));
	}

	gettimeofday(&start, NULL);
	if (!alg->ishash) {
		for (i = 0; i < count; i++) {
			cop.ses = sop.ses;
			cop.op = COP_ENCRYPT;
			cop.flags = opflags;
			cop.len = size;
			cop.src = cleartext;
			cop.dst = ciphertext;
			cop.mac = 0;
			cop.iv = iv;

			if (swap_data)
				swap_buf(cleartext, size);
			if (swap_data)
				swap_buf(ciphertext, size);
			if (swap_iv)
				swap_buf(iv, N(iv));

			if (ioctl(fd, CIOCCRYPT, &cop) < 0)
				err(1, "ioctl(CIOCCRYPT)");

			if (swap_data)
				swap_buf(cleartext, size);
			if (swap_data)
				swap_buf(ciphertext, size);
			if (swap_iv)
				swap_buf(iv, N(iv));

			if (verify && bcmp(ciphertext, cleartext, size) == 0) {
				printf("cipher text unchanged:");
				hexdump(ciphertext, size);
			}

			if (verbose) {
				printf("ciphertext:");
				hexdump(ciphertext, MIN(size, CHUNK));
				printf("cipheriv:");
				hexdump(iv, MIN(size, CHUNK));
			}

			memset(cleartext, 'x', MIN(size, CHUNK));
			cop.ses = sop.ses;
			cop.op = COP_DECRYPT;
			cop.flags = opflags;
			cop.len = size;
			cop.src = ciphertext;
			cop.dst = cleartext;
			cop.mac = 0;
			cop.iv = iv;

			if (swap_data)
				swap_buf(cleartext, size);
			if (swap_data)
				swap_buf(ciphertext, size);
			if (swap_iv)
				swap_buf(iv, N(iv));

			if (ioctl(fd, CIOCCRYPT, &cop) < 0)
				err(1, "ioctl(CIOCCRYPT)");

			if (swap_data)
				swap_buf(cleartext, size);
			if (swap_data)
				swap_buf(ciphertext, size);
			if (swap_iv)
				swap_buf(iv, N(iv));

			if (verify && bcmp(cleartext, originaltext, size) != 0) {
				printf("decrypt mismatch:\n");
				printf("original:");
				hexdump(originaltext, size);
				printf("cleartext:");
				hexdump(cleartext, size);
			}
		}
	} else {
		for (i = 0; i < count; i++) {
			cop.ses = sop.ses;
			cop.op = 0;
			cop.flags = opflags;
			cop.len = size;
			cop.src = cleartext;
			cop.dst = 0;
			cop.mac = ciphertext;
			cop.iv = 0;

			if (ioctl(fd, CIOCCRYPT, &cop) < 0)
				err(1, "ioctl(CIOCCRYPT)");

			if (verbose) {
				printf("ciphertext:");
				hexdump(ciphertext, size);
			}
		}
	}
	gettimeofday(&stop, NULL);
 
	if (ioctl(fd, CIOCFSESSION, &sop.ses) < 0)
		perror("ioctl(CIOCFSESSION)");

	if (verbose) {
		printf("cleartext:");
		hexdump(cleartext, MIN(size, CHUNK));
	}
	timersub(&stop, &start, tv);

	free(originaltext);

	close(fd);
}

#ifdef __FreeBSD__
static void
resetstats()
{
	struct cryptostats stats;
	size_t slen;

	slen = sizeof (stats);
	if (sysctlbyname("kern.crypto_stats", &stats, &slen, NULL, NULL) < 0) {
		perror("kern.crypto_stats");
		return;
	}
	bzero(&stats.cs_invoke, sizeof (stats.cs_invoke));
	bzero(&stats.cs_done, sizeof (stats.cs_done));
	bzero(&stats.cs_cb, sizeof (stats.cs_cb));
	bzero(&stats.cs_finis, sizeof (stats.cs_finis));
	stats.cs_invoke.min.tv_sec = 10000;
	stats.cs_done.min.tv_sec = 10000;
	stats.cs_cb.min.tv_sec = 10000;
	stats.cs_finis.min.tv_sec = 10000;
	if (sysctlbyname("kern.crypto_stats", NULL, NULL, &stats, sizeof (stats)) < 0)
		perror("kern.cryptostats");
}

static void
printt(const char* tag, struct cryptotstat *ts)
{
	uint64_t avg, min, max;

	if (ts->count == 0)
		return;
	avg = (1000000000LL*ts->acc.tv_sec + ts->acc.tv_nsec) / ts->count;
	min = 1000000000LL*ts->min.tv_sec + ts->min.tv_nsec;
	max = 1000000000LL*ts->max.tv_sec + ts->max.tv_nsec;
	printf("%16.16s: avg %6llu ns : min %6llu ns : max %7llu ns [%u samps]\n",
		tag, avg, min, max, ts->count);
}
#endif

static void
runtests(struct alg *alg, int count, int size, int cmd, int threads, int profile)
{
	int i, status;
	double t;
	void *region;
	struct timeval *tvp;
	struct timeval total;
	int otiming;

	if (size % alg->blocksize) {
		if (verbose)
			printf("skipping blocksize %u 'cuz not a multiple of "
				"%s blocksize %u\n",
				size, alg->name, alg->blocksize);
		return;
	}

	region = mmap(NULL, threads * sizeof (struct timeval),
			PROT_READ|PROT_WRITE, MAP_ANON|MAP_SHARED, -1, 0);
	if (region == MAP_FAILED) {
		perror("mmap");
		return;
	}
	tvp = (struct timeval *) region;
#ifdef __FreeBSD__
	if (profile) {
		size_t tlen = sizeof (otiming);
		int timing = 1;

		resetstats();
		if (sysctlbyname("debug.crypto_timing", &otiming, &tlen,
				&timing, sizeof (timing)) < 0)
			perror("debug.crypto_timing");
	}
#endif

	if (threads > 1) {
		for (i = 0; i < threads; i++)
			if (fork() == 0) {
				runtest(alg, count, size, cmd, &tvp[i]);
				exit(0);
			}
		while (waitpid(WAIT_MYPGRP, &status, 0) != -1)
			;
	} else
		runtest(alg, count, size, cmd, tvp);

	t = 0;
	for (i = 0; i < threads; i++)
		t += (((double)tvp[i].tv_sec * 1000000 + tvp[i].tv_usec) / 1000000);
	if (t) {
		int nops = alg->ishash ? count : 2*count;

		t /= threads;
		printf("%6.3lf sec, %7d %6s crypts, %7d bytes, %8.0lf byte/sec, %7.1lf Mb/sec\n",
		    t, nops, alg->name, size, (double)nops*size / t,
		    (double)nops*size / t * 8 / 1024 / 1024);
	}
#ifdef __FreeBSD__
	if (profile) {
		struct cryptostats stats;
		size_t slen = sizeof (stats);

		if (sysctlbyname("debug.crypto_timing", NULL, NULL,
				&otiming, sizeof (otiming)) < 0)
			perror("debug.crypto_timing");
		if (sysctlbyname("kern.crypto_stats", &stats, &slen, NULL, NULL) < 0)
			perror("kern.cryptostats");
		if (stats.cs_invoke.count) {
			printt("dispatch->invoke", &stats.cs_invoke);
			printt("invoke->done", &stats.cs_done);
			printt("done->cb", &stats.cs_cb);
			printt("cb->finis", &stats.cs_finis);
		}
	}
#endif
	fflush(stdout);
}

int
main(int argc, char **argv)
{
	struct alg *alg = NULL;
	int count = 1;
	int sizes[128], nsizes = 0;
	int cmd = CIOCGSESSION;
	int testall = 0;
	int maxthreads = 1;
	int profile = 0;
	int i, ch;

	srandom(time(0));

	while ((ch = getopt(argc, argv, "VKDcpzsva:bt:S:")) != -1) {
		switch (ch) {
		case 'V': swap_iv = 1; break;
		case 'K': swap_key = 1; break;
		case 'D': swap_data = 1; break;
#ifdef CIOCGSSESSION
		case 's':
			cmd = CIOCGSSESSION;
			break;
#endif
		case 'v':
			verbose++;
			break;
		case 'a':
			alg = getalgbyname(optarg);
			if (alg == NULL) {
				if (streq(optarg, "rijndael"))
					alg = getalgbyname("aes");
				else
					usage(argv[0]);
			}
			break;
		case 'S':
			srandom(atoi(optarg));
			break;
		case 't':
			maxthreads = atoi(optarg);
			break;
		case 'z':
			testall = 1;
			break;
		case 'p':
			profile = 1;
			break;
		case 'b':
			opflags |= COP_F_BATCH;
			break;
		case 'c':
			verify = 1;
			break;
		default:
			usage(argv[0]);
		}
	}
	argc -= optind, argv += optind;
	if (argc > 0)
		count = atoi(argv[0]);
	while (argc > 1) {
		int s = atoi(argv[1]);
		if (nsizes < N(sizes)) {
			sizes[nsizes++] = s;
		} else {
			printf("Too many sizes, ignoring %u\n", s);
		}
		argc--, argv++;
	}
	if (nsizes == 0) {
		if (alg)
			sizes[nsizes++] = alg->blocksize;
		else
			sizes[nsizes++] = 8;
		if (testall) {
			while (sizes[nsizes-1] < 8*1024) {
				sizes[nsizes] = sizes[nsizes-1]<<1;
				nsizes++;
			}
		}
	}

	if (testall) {
		for (i = 0; i < N(algorithms); i++) {
			int j;
			alg = &algorithms[i];
			for (j = 0; j < nsizes; j++)
				runtests(alg, count, sizes[j], cmd, maxthreads, profile);
		}
	} else {
		if (alg == NULL)
			alg = getalgbycode(CRYPTO_3DES_CBC);
		for (i = 0; i < nsizes; i++)
			runtests(alg, count, sizes[i], cmd, maxthreads, profile);
	}

	return (0);
}

void
hexdump(char *p, int n)
{
	int i, off;

	for (off = 0; n > 0; off += 16, n -= 16) {
		printf("%s%04x:", off == 0 ? "\n" : "", off);
		i = (n >= 16 ? 16 : n);
		do {
			printf(" %02x", *p++ & 0xff);
		} while (--i);
		printf("\n");
	}
}
