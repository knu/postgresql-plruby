 
   create function plruby_call_handler() returns language_handler
   as '/opt/ts/ruby/perso/plruby-0.4.3/src/plruby.so'
   language 'C';
 
   create trusted procedural language 'plruby'
	handler plruby_call_handler
        lancompiler 'PL/Ruby';
