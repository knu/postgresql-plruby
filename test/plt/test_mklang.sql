
create function plruby_call_handler() returns opaque
    as '/home2/ts/plruby-0.2.0/plruby.so'
   language 'C';

create trusted procedural language 'plruby'
	handler plruby_call_handler
	lancompiler 'PL/Ruby';
