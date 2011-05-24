require 'rubygems'
require 'rbconfig'

Gem::Specification.new do |spec|
  spec.name       = 'globegit-postgresql-plruby'
  spec.version    = '0.5.4'
  spec.authors    = ['Akinori MUSHA', 'Guy Decoux']
  spec.license    = 'Ruby'
  spec.email      = 'akinori@musha.org'
  spec.homepage   = 'https://github.com/knu/postgresql-plruby'
  spec.summary    = 'Enable Ruby for use as a procedural language within PostgreSQL'
  spec.test_files = Dir['test/test*']
  spec.extensions = ['extconf.rb']
  spec.files      = Dir['**/*'].reject{ |f| f.include?('git') || f.include?('tmp') }
  
  spec.rubyforge_project = 'plruby'

  spec.extra_rdoc_files  = [
    'README.markdown',
    'Changes'
  ] + Dir['ext/*.c']

  spec.description = <<-EOF
    PL/Ruby is a loadable procedural language for the PostgreSQL database
    system that enables the Ruby language to create functions and trigger
    procedures.
  EOF

  plruby_bin = 'plruby.' + Config::CONFIG['DLEXT']
  plruby_dir = File.join('postgresql-plruby-' + spec.version.to_s, 'src') 
  path_to_binary = File.join(Gem.dir, 'gems', plruby_dir, plruby_bin)
  
  possible_paths = Gem.path.map{ |path|
    File.join(path, 'gems', plruby_dir, plruby_bin)
  }

  spec.post_install_message = <<-EOF

    Now run the following commands from within a postgresql shell in order
    to create the plruby language on in database server:

    create function plruby_call_handler() returns language_handler
    as '#{path_to_binary}'
    language 'C';

    create trusted language 'plruby'
    handler plruby_call_handler
    lancompiler 'PL/Ruby';

    NOTE: Your actual path to #{plruby_bin} may be different. Possible
    paths to the plruby binary are:

    #{possible_paths.join("\n    ")}

  EOF
end
