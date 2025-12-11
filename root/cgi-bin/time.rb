# ========================================
# 3. RUBY - time.rb
# Zeigt Zeit und System-Info
# ========================================
#!/usr/bin/ruby

puts "Content-Type: text/html\r\n\r\n"

require 'time'

now = Time.now
uptime = `uptime`.strip rescue "unavailable"

puts <<HTML