#!/usr/bin/env ruby

source_index = ARGV.index("-c")
abort "Expected -c followed by a source file" unless source_index && ARGV[source_index + 1]

flags = []
if File.expand_path(ARGV[source_index + 1]) == File.expand_path("../src/app/mqtt2dmx.c", __dir__)
  flags << "-Dmain=mqtt2dmx_main"
end

exec("gcc", *flags, *ARGV)
