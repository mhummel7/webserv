#!/usr/bin/env ruby

puts "Content-Type: text/html\r\n\r\n"

require 'cgi'

# POST-Daten aus stdin lesen
content_length = ENV['CONTENT_LENGTH'].to_i
post_data = ""
if content_length > 0
  post_data = $stdin.read(content_length)
end

# Query-Parameter parsen mit CGI
params = {}
if !post_data.empty?
  CGI.parse(post_data).each do |key, values|
    params[key] = values.first if values && !values.empty?
  end
end

puts <<HTML
<!DOCTYPE html>
<html>
<head>
    <title>POST Test</title>
</head>
<body>
    <h1>POST Request Test</h1>
    
    <h2>Request Information</h2>
    <p><strong>REQUEST_METHOD:</strong> #{ENV['REQUEST_METHOD']}</p>
    <p><strong>CONTENT_TYPE:</strong> #{ENV['CONTENT_TYPE'] || 'none'}</p>
    <p><strong>CONTENT_LENGTH:</strong> #{content_length} bytes</p>
    
    <h3>Empfangene POST-Daten:</h3>
    <pre>#{post_data.empty? ? 'Keine POST-Daten empfangen' : post_data}</pre>
    
    <h3>Parsed Parameter:</h3>
    <ul>
HTML

params.each do |key, value|
  puts "<li><strong>#{key}:</strong> #{value}</li>"
end

puts <<HTML
    </ul>
    
    <hr>
    <h3>Test mit curl (richtig):</h3>
    <pre>curl -X POST -H "Content-Type: application/x-www-form-urlencoded" -d "name=Testuser&email=test@example.com" http://localhost:8080/root/cgi-bin/post.rb</pre>
</body>
</html>
HTML