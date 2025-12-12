#!/usr/bin/env ruby

puts "Content-Type: text/html\r\n\r\n"

puts "<!DOCTYPE html><html><head><title>Error Test</title></head><body>"
puts "<h1>Error Demonstration</h1>"

# Verschiedene Fehler-Typen (Kommentar entfernen zum Testen)

# 1. Division durch Null (RuntimeError)
puts "<h2>1. Division durch Null:</h2>"
begin
  zero = 0
  result = 100 / zero  # Hier kommt der Fehler
  puts "<p>Resultat: #{result}</p>"
rescue => e
  puts "<p style='color: red;'><strong>Fehler:</strong> #{e.class}: #{e.message}</p>"
end

rescue => e
  puts "<p style='color: red;'><strong>Fehler:</strong> #{e.message}</p>"
  puts "<h3>Backtrace:</h3>"
  puts "<pre>"
  e.backtrace.each do |line|
    puts line
  end
  puts "</pre>"
end

puts "<hr>"
puts "<h2>Server Environment:</h2>"
puts "<pre>"
ENV.each do |key, value|
  puts "#{key}: #{value}" if key.start_with?('HTTP_', 'REQUEST_', 'SERVER_')
end
puts "</pre>"

puts "</body></html>"