/*
* Copyright (c) 2015, Sveta Smirnova. All rights reserved.
* 
* This file is part of custom_hint_plugin.
*
* custom_hint_plugin is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 2 of the License, or
* (at your option) any later version.
*
* custom_hint_plugin is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with custom_hint_plugin.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MYSQL_SERVER
#define MYSQL_SERVER
#endif

#include <ctype.h>
#include <string.h>
#include <sstream>
#include <iostream>
#include <regex>

#include "sys_vars.h"

#include <mysql/plugin.h>
#include <mysql/plugin_audit.h>
#include <mysql/service_mysql_alloc.h>
#include <my_thread.h> // my_thread_handle needed by mysql_memory.h
#include <mysql/psi/mysql_memory.h>
#include <typelib.h>

using namespace std;
using namespace std::regex_constants;
static MYSQL_PLUGIN plugin_info_ptr;


/* Declarations */
enum supported_hints_t {
  JOIN_BUFFER_SIZE,
  TMP_TABLE_SIZE,
  MAX_HEAP_TABLE_SIZE,
  READ_BUFFER_SIZE,
  READ_RND_BUFFER_SIZE,
  SORT_BUFFER_SIZE,
  BULK_INSERT_BUFFER_SIZE,
  PRELOAD_BUFFER_SIZE,
  NOT_SUPPORTED
};

static std::map <my_thread_id, std::map<supported_hints_t, ulonglong> > modified_variables;

void _rewrite_query(const void *event, const struct mysql_event_parse *event_parse, char const* new_query);
supported_hints_t get_hint_switch(string hint);


/* instrument the memory allocation */
#ifdef HAVE_PSI_INTERFACE
static PSI_memory_key key_memory_custom_hint;

static PSI_memory_info all_rewrite_memory[]=
{
    { &key_memory_custom_hint, "custom_hint", 0 } 
};

static int custom_hint_plugin_init(MYSQL_PLUGIN plugin_ref)
{
  plugin_info_ptr= plugin_ref;
  const char* category= "sql";
  int count;

  count= array_elements(all_rewrite_memory);
  mysql_memory_register(category, all_rewrite_memory, count);
  return 0; /* success */
}

#else

#define plugin_init NULL
#define key_memory_custom_hint PSI_NOT_INSTRUMENTED

#endif /* HAVE_PSI_INTERFACE */


/* The job */
static int custom_hint(MYSQL_THD thd, mysql_event_class_t event_class,
                          const void *event)
{
  const struct mysql_event_parse *event_parse=
    static_cast<const struct mysql_event_parse *>(event);

  const struct mysql_event_general *event_general=
    static_cast<const struct mysql_event_general *>(event);

  if (event_parse->event_subclass == MYSQL_AUDIT_PARSE_PREPARSE)
  {

  /* 
  There are expected limitations for this regex:
  all custom hints must be specified first if you want to use them together with built-in hints
  */
  string hints= "join_buffer_size|tmp_table_size|max_heap_table_size|read_buffer_size|read_rnd_buffer_size|sort_buffer_size|bulk_insert_buffer_size|preload_buffer_size";
  regex custom_hint_clause("(\\w+\\s+/\\*\\+\\s+([^(" + hints + ")].*\\s+)*)(((" + hints + ")\\s*=\\s*\\d+\\s+)+)((.*)\\*/.*)", ECMAScript | icase);
  cmatch sm;

  cerr << "Before regex_match: " << event_parse->query.str << endl;
  cerr << "Thread id: " << thd->thread_id() << endl;

  if (regex_match(event_parse->query.str, sm, custom_hint_clause))
  {
    //replace $1$5
    cerr << "We have a match" << endl;
    std::map<supported_hints_t, ulonglong> current;
    
    for (unsigned i=0; i < sm.size(); ++i) {
      std::cout << i << "[" << sm[i] << "] ";
    }
    cout << endl;
    
    regex matches("\\s*(" + hints + ")\\s*=\\s*(\\d+)(.*)*", ECMAScript | icase);
    string subpart= sm[3];
    smatch ssm;
    while (regex_match(subpart, ssm, matches))
    {
      cout << "innder match" << endl;
      
      for (unsigned i=0; i < ssm.size(); ++i) {
        std::cout << i << "[" << ssm[i] << "] ";
      }
      cout << endl;
      
      switch(get_hint_switch(ssm[1]))
      {
        case JOIN_BUFFER_SIZE:
          current[JOIN_BUFFER_SIZE]= thd->variables.join_buff_size;
          thd->variables.join_buff_size= stoull(ssm[2]);
          break;
        case TMP_TABLE_SIZE:
          current[TMP_TABLE_SIZE]= thd->variables.tmp_table_size;
          thd->variables.tmp_table_size= stoull(ssm[2]);
          break;
        case MAX_HEAP_TABLE_SIZE:
          current[MAX_HEAP_TABLE_SIZE]= thd->variables.max_heap_table_size;
          thd->variables.max_heap_table_size= stoull(ssm[2]);
          break;
        case READ_BUFFER_SIZE:
          current[READ_BUFFER_SIZE]= thd->variables.read_buff_size;
          thd->variables.read_buff_size= stoull(ssm[2]);
          break;
        case READ_RND_BUFFER_SIZE:
          current[JOIN_BUFFER_SIZE]= thd->variables.read_rnd_buff_size;
          thd->variables.read_rnd_buff_size= stoull(ssm[2]);
          break;
        case SORT_BUFFER_SIZE:
          current[SORT_BUFFER_SIZE]= thd->variables.sortbuff_size;
          thd->variables.sortbuff_size= stoull(ssm[2]);
          break;
        case BULK_INSERT_BUFFER_SIZE:
          current[BULK_INSERT_BUFFER_SIZE]= thd->variables.bulk_insert_buff_size;
          thd->variables.bulk_insert_buff_size= stoull(ssm[2]);
          break;
        case PRELOAD_BUFFER_SIZE:
          current[PRELOAD_BUFFER_SIZE]= thd->variables.preload_buff_size;
          thd->variables.preload_buff_size= stoull(ssm[2]);
          break;
        default:
          cerr << ssm[1] << "=" << ssm[2] << endl;
      }
      subpart= ssm[3]; //regex_replace(subpart, matches, "$3");
    }
    
    //erase first if set, better in the beginning, before regex
    modified_variables[thd->thread_id()]= current;
    
    string rewritten_query;
    rewritten_query= regex_replace(event_parse->query.str, custom_hint_clause, "$1$6");
    _rewrite_query(event, event_parse, rewritten_query.c_str());
    
    cerr << "Rewritten query: " << rewritten_query << endl;
  }

  }
  
  if (event_general->event_subclass == MYSQL_AUDIT_GENERAL_RESULT)
  {
    cerr << "Catching result set" << endl;
    std::map<my_thread_id, std::map<supported_hints_t, ulonglong> >::iterator current= modified_variables.find(thd->thread_id());
    if (current != modified_variables.end())
    {
       cerr << "Map exists" << endl;
       
       for (std::map<supported_hints_t, ulonglong>::iterator it= current->second.begin(); it!= current->second.end(); ++it)
       {
         cerr << it->first << ": " << it->second << endl;
         switch(it->first)
         {
         case JOIN_BUFFER_SIZE:
           thd->variables.join_buff_size= it->second;
           break;
         case TMP_TABLE_SIZE:
           thd->variables.tmp_table_size= it->second;
           break;
         case MAX_HEAP_TABLE_SIZE:
           thd->variables.max_heap_table_size= it->second;
           break;
         case READ_BUFFER_SIZE:
           thd->variables.read_buff_size= it->second;
           break;
         case READ_RND_BUFFER_SIZE:
           thd->variables.read_rnd_buff_size= it->second;
           break;
         case SORT_BUFFER_SIZE:
           thd->variables.sortbuff_size= it->second;
           break;
         case BULK_INSERT_BUFFER_SIZE:
           thd->variables.bulk_insert_buff_size= it->second;
           break;
         case PRELOAD_BUFFER_SIZE:
           thd->variables.preload_buff_size= it->second;
           break;
         default:
           cerr << "Not supported yet" << endl;
         }
       }
       
       modified_variables.erase(current);
    }
  } 

  return 0;
}

static st_mysql_audit custom_hint_plugin_descriptor= {
  MYSQL_AUDIT_INTERFACE_VERSION,  /* interface version */
  NULL,
  custom_hint,                         /* implements custom hints */
  { (unsigned long) MYSQL_AUDIT_GENERAL_ALL, 0, (unsigned long) MYSQL_AUDIT_PARSE_ALL,}
};

mysql_declare_plugin(custom_hint_plugin)
{
  MYSQL_AUDIT_PLUGIN,
  &custom_hint_plugin_descriptor,
  "custom_hint_plugin",
  "Sveta Smirnova",
  "Custom Optimizer hints for MySQL (not available with standard \"Optimizer Hints\" feature)",
  PLUGIN_LICENSE_GPL,
  custom_hint_plugin_init,
  NULL,                         /* custom_hint_plugin_deinit - TODO */
  0x0001,                       /* version 0.0.1      */
  NULL,                         /* status variables   */
  NULL,                         /* system variables   */
  NULL,                         /* config options     */
  0,                            /* flags              */
}
mysql_declare_plugin_end;


// private functions

void _rewrite_query(const void *event, const struct mysql_event_parse *event_parse, char const* new_query)
{
  char *rewritten_query= static_cast<char *>(my_malloc(key_memory_custom_hint, strlen(new_query) + 1, MYF(0)));
  strncpy(rewritten_query, new_query, strlen(new_query));
  rewritten_query[strlen(new_query)]= '\0';
  event_parse->rewritten_query->str= rewritten_query;
  event_parse->rewritten_query->length=strlen(new_query);
  *((int *)event_parse->flags)|= (int)MYSQL_AUDIT_PARSE_REWRITE_PLUGIN_QUERY_REWRITTEN;
};


supported_hints_t get_hint_switch(string hint)
{
  if (0 == hint.compare("join_buffer_size"))
    return JOIN_BUFFER_SIZE;
  if (0 == hint.compare("tmp_table_size"))
    return TMP_TABLE_SIZE;
  if (0 == hint.compare("read_buffer_size"))
    return READ_BUFFER_SIZE;
  return NOT_SUPPORTED;
};


/* vim: set tabstop=2 shiftwidth=2 softtabstop=2: */
/* 
* :tabSize=2:indentSize=2:noTabs=true:
* :folding=custom:collapseFolds=1: 
*/
