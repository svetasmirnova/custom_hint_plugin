# custom_hint_plugin
Introduction
============

This plugin provides custom hints which can affect work of MySQL Optimizer. Design is similar to [Optimizer Hints] (http://), uses same style of comments, but practically changes values of thread-specific buffers which can affect query optimization and execution. After query finishes buffer values returned to previous state.

Compiling
=========

Place plugin directory into plugin subdirectory of MySQL source directory, then cd to MySQL source directory and compile server. Plugin will be compiled automatically. You need to use GCC 4.9 or newer  or any other compiler which supports [std::regex] () to compile plugin.

Installation
============

Copy custom_hint_plugin.so into plugin directory of your MySQL server, then login and type:

    INSTALL PLUGIN custom_hint_plugin SONAME 'custom_hint_plugin.so';

Uninstallation
==============

Connect to MySQL server and run:

    UNINSTALL PLUGIN custom_hint_plugin;
    
Usage examples
==============

    select /*+ join_buffer_size=16384 */ count(*) from t1 join t2 using(f1);
    select /*+ join_buffer_size=16384 tmp_table_size=1024 max_heap_table_size=16384 */ count(*) from t1 join t2 using(f1);
    select /*+ join_buffer_size=16384 BKA(t1) NO_BKA(t2) */ count(*) from t1 join t2 using(f1);
    
Limitations
===========

If you want to use custom hints together with built-in optimizer hints you have to put custom hints in the beginning of the comment.
