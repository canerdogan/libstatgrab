/*
 * i-scream central monitoring system
 * http://www.i-scream.org
 * Copyright (C) 2000-2004 i-scream
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA
 *
 * $Id$
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "statgrab.h"
#if defined(SOLARIS) || defined(LINUX)
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#endif

#ifdef SOLARIS
#include <procfs.h>
#include <limits.h>
#define PROC_LOCATION "/proc"
#define MAX_FILE_LENGTH PATH_MAX
#endif
#ifdef LINUX
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#define PROC_LOCATION "/proc"
#define MAX_FILE_LENGTH PATH_MAX
#endif
#ifdef ALLBSD
#include <stdlib.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#if defined(FREEBSD) || defined(DFBSD)
#include <sys/user.h>
#else
#include <sys/proc.h>
#endif
#include <string.h>
#include <paths.h>
#include <fcntl.h>
#include <limits.h>
#include <kvm.h>
#endif

int get_proc_snapshot(proc_state_t **ps){
	proc_state_t *proc_state = NULL;
	proc_state_t *proc_state_ptr;
	int proc_state_size = 0;
#ifdef ALLBSD
	int mib[3];
	size_t size;
	struct kinfo_proc *kp_stats;
	int procs, i, alloc;
	static kvm_t *kvmd;
	char **args;
	char *proctitle;
#endif
#if defined(SOLARIS) || defined(LINUX)
        DIR *proc_dir;
        struct dirent *dir_entry;
        char filename[MAX_FILE_LENGTH];
        FILE *f;
#ifdef SOLARIS
	psinfo_t process_info;
#endif
#ifdef LINUX
	char s;
	/* If someone has a executable of 4k filename length, they deserve to get it truncated :) */
	char ps_name[4096];
	char *ptr;
	static char *psargs = NULL;
	static int psarg_size = 0;
	unsigned long stime, utime;
	int x;
	int fn;
	int toread;
	ssize_t size;
	int t_read;
#endif

        if((proc_dir=opendir(PROC_LOCATION))==NULL){
                return -1;
        }

        while((dir_entry=readdir(proc_dir))!=NULL){
                if(atoi(dir_entry->d_name) == 0) continue;

#ifdef SOLARIS
                snprintf(filename, MAX_FILE_LENGTH, "/proc/%s/psinfo", dir_entry->d_name);
#endif
#ifdef LINUX
		snprintf(filename, MAX_FILE_LENGTH, "/proc/%s/stat", dir_entry->d_name);
#endif
                if((f=fopen(filename, "r"))==NULL){
                        /* Open failed.. Process since vanished, or the path was too long.
                         * Ah well, move onwards to the next one */
                        continue;
                }
#ifdef SOLARIS
                fread(&process_info, sizeof(psinfo_t), 1, f);
#endif

		proc_state = realloc(proc_state, (1+proc_state_size)*sizeof(proc_state_t));
		proc_state_ptr = proc_state+proc_state_size;
#ifdef SOLARIS		
		proc_state_ptr->pid = process_info.pr_pid;
		proc_state_ptr->parent = process_info.pr_ppid;
		proc_state_ptr->pgid = process_info.pr_pgid;
		proc_state_ptr->uid = process_info.pr_uid;
		proc_state_ptr->euid = process_info.pr_euid;
		proc_state_ptr->gid = process_info.pr_gid;
		proc_state_ptr->egid = process_info.pr_egid;
		proc_state_ptr->proc_size = (process_info.pr_size) * 1024;
		proc_state_ptr->proc_resident = (process_info.pr_rssize) * 1024;
		proc_state_ptr->time_spent = process_info.pr_time.tv_sec;
		proc_state_ptr->cpu_percent = (process_info.pr_pctcpu * 100.0) / 0x8000;
		proc_state_ptr->process_name = strdup(process_info.pr_fname);
		proc_state_ptr->proctitle = strdup(process_info.pr_psargs);

                if(process_info.pr_lwp.pr_state==1) proc_state_ptr->state = SLEEPING;
                if(process_info.pr_lwp.pr_state==2) proc_state_ptr->state = RUNNING; 
                if(process_info.pr_lwp.pr_state==3) proc_state_ptr->state = ZOMBIE; 
                if(process_info.pr_lwp.pr_state==4) proc_state_ptr->state = STOPPED; 
                if(process_info.pr_lwp.pr_state==6) proc_state_ptr->state = RUNNING; 
#endif
#ifdef LINUX
		x = fscanf(f, "%d %4096s %c %d %d %*d %*d %*d %*lu %*lu %*lu %*lu %*lu %lu %lu %*ld %*ld %*ld %d %*ld %*ld %*lu %llu %llu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %*lu %*d %*d\n", &(proc_state_ptr->pid), ps_name, &s, &(proc_state_ptr->parent), &(proc_state_ptr->pgid), &utime, &stime, &(proc_state_ptr->nice), &(proc_state_ptr->proc_size), &(proc_state_ptr->proc_resident));
		proc_state_ptr->proc_resident = proc_state_ptr->proc_resident * getpagesize();
		if(s == 'S') proc_state_ptr->state = SLEEPING;
		if(s == 'R') proc_state_ptr->state = RUNNING;
		if(s == 'Z') proc_state_ptr->state = ZOMBIE;
		if(s == 'T') proc_state_ptr->state = STOPPED;
		if(s == 'D') proc_state_ptr->state = STOPPED;
	
		/* pa_name[0] should = '(' */
		ptr = strchr(&ps_name[1], ')');	
		if(ptr !=NULL) *ptr='\0';
		proc_state_ptr->process_name = strdup(&ps_name[1]);

		/* Need to do cpu */
		

		/* proctitle */	
		snprintf(filename, MAX_FILE_LENGTH, "/proc/%s/cmdline", dir_entry->d_name);

                if((fn=open(filename, O_RDONLY)) == -1){
                        /* Open failed.. Process since vanished, or the path was too long.
                         * Ah well, move onwards to the next one */
                        continue;
                }
#define		PSARG_START_SIZE 128
		if(psargs == NULL){
			psargs = malloc(PSARG_START_SIZE);
			psarg_size = PSARG_START_SIZE;
		}
		ptr = psargs;	
		t_read = 0;
		toread = psarg_size;
		while((size = read(fn, ptr, toread)) == toread){
			psargs = realloc(psargs, (psarg_size + PSARG_START_SIZE));
			ptr = psargs+psarg_size;
			t_read = psarg_size;
			psarg_size+=PSARG_START_SIZE;
			toread = PSARG_START_SIZE;
		}
		if(size != -1) t_read+=size;

		ptr = psargs;
		for(x=0; x<t_read; x++){
			if (*ptr == '\0') *ptr = ' ';
			ptr++;
		}
		/*  for safety sake */
		psargs[t_read] = '\0';

		proc_state_ptr->proctitle = strdup(psargs);

#endif

		proc_state_size++;

                fclose(f);
        }
        closedir(proc_dir);
#endif

#ifdef ALLBSD
	kvmd = kvm_openfiles(_PATH_DEVNULL, _PATH_DEVNULL, NULL, O_RDONLY, NULL);

	if(kvmd == NULL) return NULL;

	kp_stats = kvm_getprocs(kvmd, KERN_PROC_ALL, 0, &procs);

	if (kp_stats == NULL || procs < 0) {
		return NULL;
	}

	for (i = 0; i < procs; i++) {
		/* replace with something more sensible */
		proc_state = realloc(proc_state,
				(1+proc_state_size)*sizeof(proc_state_t));
		if(proc_state == NULL ) {
			return NULL;
		}
		proc_state_ptr = proc_state+proc_state_size;

#ifdef FREEBSD5
		proc_state_ptr->process_name =
			strdup(kp_stats[i].ki_comm);
#else
		proc_state_ptr->process_name =
			strdup(kp_stats[i].kp_proc.p_comm);
#endif

		args = kvm_getargv(kvmd, &(kp_stats[i]), 0);
		if(args != NULL) {
			alloc = 1;
			proctitle = malloc(alloc);
			if(proctitle == NULL) {
				return NULL;
			}
			while(*args != NULL) {
				if(strlen(proctitle) + strlen(*args) >= alloc) {
					alloc = (alloc + strlen(*args)) << 1;
					proctitle = realloc(proctitle, alloc);
					if(proctitle == NULL) {
						return NULL;
					}
				}
				strncat(proctitle, *args, strlen(*args));
				strncat(proctitle, " ", 1);
				args++;
			}
			/* remove trailing space */
			proctitle[strlen(proctitle)-1] = NULL;
			proc_state_ptr->proctitle = proctitle;
		}
		else {
			proc_state_ptr->proctitle =
				malloc(strlen(proc_state_ptr->process_name)+4);
			if(proc_state_ptr->proctitle == NULL) {
				return NULL;
			}
			sprintf(proc_state_ptr->proctitle, " (%s)",
				proc_state_ptr->process_name);
		}

#ifdef FREEBSD5
		proc_state_ptr->pid = kp_stats[i].ki_pid;
		proc_state_ptr->parent = kp_stats[i].ki_ppid;
		proc_state_ptr->pgid = kp_stats[i].ki_pgid;
#else
		proc_state_ptr->pid = kp_stats[i].kp_proc.p_pid;
		proc_state_ptr->parent = kp_stats[i].kp_eproc.e_ppid;
		proc_state_ptr->pgid = kp_stats[i].kp_eproc.e_pgid;
#endif

#ifdef FREEBSD5
		proc_state_ptr->uid = kp_stats[i].ki_ruid;
		proc_state_ptr->euid = kp_stats[i].ki_uid;
		proc_state_ptr->gid = kp_stats[i].ki_rgid;
		proc_state_ptr->egid = kp_stats[i].ki_svgid;
#else
		proc_state_ptr->uid = kp_stats[i].kp_eproc.e_pcred.p_ruid;
		proc_state_ptr->euid = kp_stats[i].kp_eproc.e_pcred.p_svuid;
		proc_state_ptr->gid = kp_stats[i].kp_eproc.e_pcred.p_rgid;
		proc_state_ptr->egid = kp_stats[i].kp_eproc.e_pcred.p_svgid;
#endif

#ifdef FREEBSD5
		proc_state_ptr->proc_size = kp_stats[i].ki_size;
		/* This is in pages */
		proc_state_ptr->proc_resident =
			kp_stats[i].ki_rssize * getpagesize();
		/* This is in microseconds */
		proc_state_ptr->time_spent = kp_stats[i].ki_runtime / 1000000;
		proc_state_ptr->cpu_percent =
			((double)kp_stats[i].ki_pctcpu / FSCALE) * 100.0;
		proc_state_ptr->nice = kp_stats[i].ki_nice;
#else
		proc_state_ptr->proc_size =
			kp_stats[i].kp_eproc.e_vm.vm_map.size;
		/* This is in pages */
		proc_state_ptr->proc_resident =
			kp_stats[i].kp_eproc.e_vm.vm_rssize * getpagesize();
		/* This is in microseconds */
		proc_state_ptr->time_spent =
			kp_stats[i].kp_proc.p_runtime / 1000000;
		proc_state_ptr->cpu_percent =
			((double)kp_stats[i].kp_proc.p_pctcpu / FSCALE) * 100.0;
		proc_state_ptr->nice = kp_stats[i].kp_proc.p_nice;
#endif

#ifdef FREEBSD5
		switch (kp_stats[i].ki_stat) {
#else
		switch (kp_stats[i].kp_proc.p_stat) {
#endif
		case SIDL:
		case SRUN:
#ifdef SONPROC
		case SONPROC: /* NetBSD */
#endif
			proc_state_ptr->state = RUNNING;
			break;
		case SSLEEP:
#ifdef SWAIT
		case SWAIT: /* FreeBSD 5 */
#endif
#ifdef SLOCK
		case SLOCK: /* FreeBSD 5 */
#endif
			proc_state_ptr->state = SLEEPING;
			break;
		case SSTOP:
			proc_state_ptr->state = STOPPED;
			break;
		case SZOMB:
#ifdef SDEAD
		case SDEAD: /* OpenBSD & NetBSD */
#endif
			proc_state_ptr->state = ZOMBIE;
			break;
		default:
			proc_state_ptr->state = UNKNOWN;
			break;
		}
		proc_state_size++;
	}

	free(kp_stats);
#endif

	*ps = proc_state;
	return proc_state_size;
}

process_stat_t *get_process_stats() {
	static process_stat_t process_stat;
	proc_state_t *ps;
	int ps_size, x;

	process_stat.sleeping = 0;
	process_stat.running = 0;
	process_stat.zombie = 0;
	process_stat.stopped = 0;
	process_stat.total = 0;

	ps_size = get_proc_snapshot(&ps);

	if(ps_size == NULL) {
		return NULL;
	}

	for(x = 0; x < ps_size; x++) {
		switch (ps->state) {
		/* currently no mapping for UNKNOWN in process_stat_t */
		case RUNNING:
			process_stat.running++;
			break;
		case SLEEPING:
			process_stat.sleeping++;
			break;
		case STOPPED:
			process_stat.stopped++;
			break;
		case ZOMBIE:
			process_stat.zombie++;
			break;
		}
		ps++;
	}

	process_stat.total = ps_size;

	return &process_stat;
}
