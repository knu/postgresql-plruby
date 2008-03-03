
   create function plruby_call_handler() returns language_handler
   as '/home/ts/ruby/devel/plruby-0.5.2/src/plruby.so'
   language 'C';

   create trusted  language 'plruby'
	handler plruby_call_handler
	lancompiler 'PL/Ruby';
