require "json"

selector = ARGV.empty? ? "test:all" : ARGV.first
unless ARGV.size <= 1 && selector.match?(/\Atest:[a-zA-Z0-9_]+\z/)
  abort "Expected one selector: test:all or test:<module>"
end

unless system("bundle", "exec", "ceedling", "clobber", selector)
  exit($?.exitstatus || 1)
end

report_path = "build/artifacts/test/tests_report.json"
abort "No test report was generated" unless File.file?(report_path)
summary = JSON.parse(File.read(report_path)).fetch("Summary")
unless summary.fetch("total_tests").positive? &&
       summary.fetch("passed") == summary.fetch("total_tests") &&
       summary.fetch("ignored").zero? && summary.fetch("failures").zero?
  abort "Expected a nonempty suite with every test passing: #{summary}"
end
