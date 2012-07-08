require 'rake'
require 'rake/testtask'
require 'rake/clean'
require 'rbconfig'
include Config

# TODO: Reorganize code to use Rake compiler.

CLEAN.include(
  '**/*.gem',               # Gem files
  '**/*.rbc',               # Rubinius
  '**/*.o',                 # C object file
  '**/*.log',               # Ruby extension build log
  '**/Makefile',            # C Makefile
  '**/conftest.dSYM',       # OS X build directory
  "**/*.#{CONFIG['DLEXT']}" # C shared object
)

desc 'Build the postgresql-plruby source code'
task :build => [:clean] do
  ruby "extconf.rb"
  sh "make"
end

namespace :gem do
  desc 'Create the gem'
  task :create => [:clean] do
    spec = eval(IO.read('postgresql-plruby.gemspec'))
    Gem::Builder.new(spec).build
  end

  desc 'Install the gem'
  task :install => [:create] do
    file = Dir["*.gem"].first
    sh "gem install #{file}" 
  end
end

# TODO: Reorganize tests to make them work.
Rake::TestTask.new do |t|
  task :test => [:build]
  t.libs << 'ext'
  t.test_files = FileList['test/**/*.rb']
  t.warning = true
  t.verbose = true
end

task :default => :test
