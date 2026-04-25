/*
*
* SPDX-License-Identifier: GPL-2.0
*
* Copyright (C) 2020 Amlogic or its affiliates
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; version 2.
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
* or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
* for more details.
* You should have received a copy of the GNU General Public License along
* with this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*/

#ifndef _AML_LOG_H_
#define _AML_LOG_H_

#include <stdarg.h>
#include <linux/printk.h>

extern int debug_show_sof;

#if 0
extern unsigned int aml_cam_log_level;
#define AML_CAM_LOG_LEVEL_DEBUG 1
#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "aml-cam-debug: " fmt

#define aml_cam_log_info(fmt, ...) \
	pr_info(fmt, ##__VA_ARGS__)

#define aml_cam_log_err(fmt, ...) \
	pr_err(fmt, ##__VA_ARGS__)

#define aml_cam_log_dbg(fmt, ...) \
	do { \
		if ((aml_cam_log_level & 0xff) >= AML_CAM_LOG_LEVEL_DEBUG) { \
			pr_info(fmt, ##__VA_ARGS__); \
		} \
	} while (0)
#endif
#endif

