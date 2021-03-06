/*
 * Copyright (C) 2015-2016 OpenSIPS Solutions
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 *
 * History:
 * --------
 *  2015-10-01 initial version (Ionel Cerghit)
 */

#include <dlfcn.h>
#include <string.h>

#include "module_info.h"
#include "../dprint.h"
#include "shm_mem.h"
#include "common.h"

char buff[60];


unsigned int mem_free_idx = 1;
struct multi_str* mod_names = NULL;
void* main_handle = NULL;
volatile struct module_info* memory_mods_stats = NULL;
int core_index;

int set_mem_idx(char* mod_name, int  mem_free_idx){

	int *var;
	if(!main_handle){
		main_handle = dlopen(NULL, RTLD_LAZY);
		if(!main_handle){
			LM_CRIT("could not load main binary handle\n");
			return -1;
		}
	}

	if(strlen(mod_name) == 4 && (strncmp("core", mod_name, 4) == 0))
		core_index = mem_free_idx;

	strcpy(buff, mod_name);
	strcat(buff, STAT_SUFIX);

	var = (int*) dlsym(main_handle, buff);

	if(!var){
		LM_CRIT("The module %s was not found, be sure it is the same as the NAME variable from the module's Makefile"
			    "(without .so) and run 'make generate-mem-stats'\n", mod_name);
		return -1;
	}

	*var = mem_free_idx;
	LM_DBG("changed module variable %s = %d\n", buff, *var);

	return 0;
}

int init_new_stat(stat_var* stat) {
#ifndef DBG_MALLOC
	#define MALLOC_UNSAFE(_size)  MY_MALLOC_UNSAFE(shm_block, _size)
#else
	#define MALLOC_UNSAFE(_size)  MY_MALLOC_UNSAFE(shm_block, _size, __FILE__, __FUNCTION__, __LINE__ )
#endif

	stat->u.val = MALLOC_UNSAFE(sizeof(stat_val));

	if(!stat->u.val) {
		LM_ERR("no more shm memory\n");
		return -1;
	}

#ifdef NO_ATOMIC_OPS
		*(stat->u.val) = 0;
#else
		atomic_set(stat->u.val,0);
#endif

	return 0;

#undef MALLOC_UNSAFE
}

inline void update_module_stats(long mem_used, long real_used, int frags, int group_idx) {
#ifdef SHM_SHOW_DEFAULT_GROUP
	if(!memory_mods_stats)
		return;
	update_stat(&memory_mods_stats[group_idx].fragments, frags);
	update_stat(&memory_mods_stats[group_idx].memory_used, mem_used);
	update_stat(&memory_mods_stats[group_idx].real_used, real_used);
#else
	if(group_idx == 0)
		return;
	update_stat(&memory_mods_stats[group_idx - 1].fragments, frags);
	update_stat(&memory_mods_stats[group_idx - 1].memory_used, mem_used);
	update_stat(&memory_mods_stats[group_idx - 1].real_used, real_used);
#endif
}

int alloc_group_stat(void) {
	int size_prealoc, groups;

#ifndef SHM_SHOW_DEFAULT_GROUP
	groups = mem_free_idx - 1;
#else
	groups = mem_free_idx;

#endif

	size_prealoc = groups * sizeof(struct module_info);

#ifndef DBG_MALLOC
	memory_mods_stats = MY_REALLOC_UNSAFE(shm_block, (void*)memory_mods_stats, size_prealoc);
#else
	memory_mods_stats = MY_REALLOC_UNSAFE(shm_block, (void*)memory_mods_stats, size_prealoc, __FILE__, __FUNCTION__, __LINE__ );
#endif

	if(!memory_mods_stats){
		LM_CRIT("could not alloc shared memory");
		return -1;
	}
	//initialize the new created group
	memset((void*)&memory_mods_stats[groups - 1], 0, sizeof(struct module_info));
	if (init_new_stat((stat_var *)&memory_mods_stats[groups - 1].fragments) < 0)
		return -1;

	if (init_new_stat((stat_var *)&memory_mods_stats[groups - 1].memory_used) < 0)
		return -1;

	if (init_new_stat((stat_var *)&memory_mods_stats[groups - 1].real_used) < 0)
		return -1;

	if(core_index) {
		if(mem_free_idx - 1 == core_index) {
			update_module_stats(size_prealoc + groups * sizeof(stat_val) * 3, size_prealoc +
							groups * sizeof(stat_val) * 3 + FRAG_OVERHEAD * (groups * 3 + 1), groups * 3 + 1, core_index);
			update_module_stats(-((groups - 1) * sizeof(struct module_info) + (groups - 1) * sizeof(stat_val) * 3),
			-((groups - 1) * sizeof(struct module_info) + (groups - 1) * sizeof(stat_val) * 3 + FRAG_OVERHEAD * ((groups - 1) * 3 + 1)),
			  -((groups - 1) * 3 + 1), 0);
		} else {
			update_module_stats(sizeof(stat_val) * 3 + sizeof(struct module_info), sizeof(stat_val) * 3 + sizeof(struct module_info) 
							+ 3 * FRAG_OVERHEAD, 3, core_index);
		}
	} else {
#ifdef SHM_SHOW_DEFAULT_GROUP
	update_stat(&memory_mods_stats[0].fragments, 3);
	update_stat(&memory_mods_stats[0].memory_used, sizeof(stat_val) * 3 + sizeof(struct module_info));
	update_stat(&memory_mods_stats[0].real_used, sizeof(stat_val) * 3 + sizeof(struct module_info)
							+ 3 * FRAG_OVERHEAD);
#endif
	}


	return 0;
}
