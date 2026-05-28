// Copyright 2021 Rockchip Electronics Co., Ltd. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "iniparser.h"
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h> // PRId64
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define log_debug(fmt, args...) fprintf(stderr, "[DBG] " fmt, ##args)
#define log_info(fmt, args...)  fprintf(stderr, "[INFO] " fmt, ##args)
#define log_error(fmt, args...) fprintf(stderr, "[ERR] " fmt, ##args)

#define MAX_SECTION_KEYS 1024

char g_ini_path_[256];
dictionary *g_ini_d_;
static pthread_mutex_t g_param_mutex = PTHREAD_MUTEX_INITIALIZER;

int rk_param_dump() {
	const char *section_name;
	const char *keys[MAX_SECTION_KEYS];
	int section_keys;
	int section_num = iniparser_getnsec(g_ini_d_);
	log_debug("section_num is %d\n", section_num);

	for (int i = 0; i < section_num; i++) {
		section_name = iniparser_getsecname(g_ini_d_, i);
		section_keys = iniparser_getsecnkeys(g_ini_d_, section_name);
		log_debug("section_name is %s, section_keys is %d\n", section_name, section_keys);
		for (int j = 0; j < section_keys; j++) {
			iniparser_getseckeys(g_ini_d_, section_name, keys);
			log_debug("%s = %s\n", keys[j], iniparser_getstring(g_ini_d_, keys[j], ""));
		}
	}

	return 0;
}

int rk_param_save() {
	FILE *fp = fopen(g_ini_path_, "w");
	if (fp == NULL) {
		log_error("%s, fopen error!\n", g_ini_path_);
		iniparser_freedict(g_ini_d_);
		g_ini_d_ = NULL;
		return -1;
	}
	iniparser_dump_ini(g_ini_d_, fp);

	fflush(fp);
	fclose(fp);

	return 0;
}

int rk_param_get_int(const char *entry, int default_val) {
	int ret;
	pthread_mutex_lock(&g_param_mutex);
	ret = iniparser_getint(g_ini_d_, entry, default_val);
	pthread_mutex_unlock(&g_param_mutex);

	return ret;
}

double rk_param_get_double(const char *entry, double default_val) {
	double ret;
	pthread_mutex_lock(&g_param_mutex);
	ret = iniparser_getdouble(g_ini_d_, entry, default_val);
	pthread_mutex_unlock(&g_param_mutex);

	return ret;
}

int rk_param_set_int(const char *entry, int val) {
	char tmp[8];
	sprintf(tmp, "%d", val);
	pthread_mutex_lock(&g_param_mutex);
	iniparser_set(g_ini_d_, entry, tmp);
	pthread_mutex_unlock(&g_param_mutex);

	return 0;
}

const char *rk_param_get_string(const char *entry, const char *default_val) {
	const char *ret;
	pthread_mutex_lock(&g_param_mutex);
	ret = iniparser_getstring(g_ini_d_, entry, default_val);
	pthread_mutex_unlock(&g_param_mutex);

	return ret;
}

int rk_param_set_string(const char *entry, const char *val) {
	pthread_mutex_lock(&g_param_mutex);
	iniparser_set(g_ini_d_, entry, val);
	pthread_mutex_unlock(&g_param_mutex);

	return 0;
}

int rk_param_init(char *ini_path) {
	log_debug("%s\n", __func__);
	char cmd[256];
	pthread_mutex_lock(&g_param_mutex);
	g_ini_d_ = NULL;
	if (ini_path) {
		memcpy(g_ini_path_, ini_path, strlen(ini_path));
	} else {
		log_error("ini_path is empty!");
		return -1;
	}
	log_info("g_ini_path_ is %s\n", g_ini_path_);

	g_ini_d_ = iniparser_load(g_ini_path_);
	if (g_ini_d_ == NULL) {
		log_error("iniparser_load error!\n");
		pthread_mutex_unlock(&g_param_mutex);
		return -1;
	}
	rk_param_dump();
	pthread_mutex_unlock(&g_param_mutex);

	return 0;
}

int rk_param_deinit() {
	log_info("%s\n", __func__);
	if (g_ini_d_ == NULL)
		return 0;
	pthread_mutex_lock(&g_param_mutex);
	rk_param_save();
	if (g_ini_d_)
		iniparser_freedict(g_ini_d_);
	pthread_mutex_unlock(&g_param_mutex);

	return 0;
}

int rk_param_reload() {
	log_info("%s\n", __func__);
	pthread_mutex_lock(&g_param_mutex);
	if (g_ini_d_)
		iniparser_freedict(g_ini_d_);
	g_ini_d_ = iniparser_load(g_ini_path_);
	if (g_ini_d_ == NULL) {
		log_error("iniparser_load error!\n");
		pthread_mutex_unlock(&g_param_mutex);
		return -1;
	}
	rk_param_dump();
	pthread_mutex_unlock(&g_param_mutex);

	return 0;
}
