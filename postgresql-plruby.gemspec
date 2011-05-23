require 'rubygems'

Gem::Specification.new do |spec|
  spec.name       = 'postgresql-plruby'
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
end
