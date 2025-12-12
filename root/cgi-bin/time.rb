#!/usr/bin/env ruby

puts "Content-Type: text/html\r\n\r\n"

require 'time'

now = Time.now

puts <<HTML
<!DOCTYPE html>
<html>
<head><title>Aktuelle Zeit</title></head>
<body>
    <h1>Aktuelle Server-Zeit</h1>
    
    <h2>#{now.strftime("%A, %d. %B %Y")}</h2>
    <h3>#{now.strftime("%H:%M:%S")} (#{now.zone})</h3>
    
    <h4>Weitere Formate:</h4>
    <ul>
        <li>ISO 8601: #{now.iso8601}</li>
        <li>RFC 2822: #{now.rfc2822}</li>
        <li>UTC: #{now.utc}</li>
        <li>Unix Timestamp: #{now.to_i}</li>
    </ul>
    
    <p><em>Generiert von Ruby CGI</em></p>
</body>
</html>
HTML