
   create function plruby_call_handler() returns language_handler
    as '/home/ts/ruby/perso/plruby-0.3.5/plruby.so'
   language 'C';

   create trusted  language 'plruby'
	handler plruby_call_handler
	lancompiler 'PL/Ruby';
