#!/usr/bin/env ruby

puts "Content-Type: text/html\r\n\r\n"

# POST-Daten aus stdin lesen
content_length = ENV['CONTENT_LENGTH'].to_i
post_data = ""
if content_length > 0
  post_data = $stdin.read(content_length)
end

# Query-Parameter parsen
params = {}
if !post_data.empty? && ENV['CONTENT_TYPE'] == 'application/x-www-form-urlencoded'
  post_data.split('&').each do |pair|
    key, value = pair.split('=')
    params[URI.decode_www_form_component(key || '')] = URI.decode_www_form_component(value || '') if key
  end
end

puts <<HTML
<!DOCTYPE html>
<html>
<head>
    <title>POST Test</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .form-box { background: #f0f0f0; padding: 20px; border-radius: 10px; }
        input, textarea { margin: 10px 0; padding: 8px; width: 300px; }
        .result { background: #e0ffe0; padding: 15px; margin: 20px 0; border-radius: 5px; }
    </style>
</head>
<body>
    <h1>POST Request Test</h1>
    
    <div class="form-box">
        <h2>Testformular</h2>
        <form method="POST" action="post.rb">
            <div>
                <label>Name: <input type="text" name="name"></label>
            </div>
            <div>
                <label>Email: <input type="email" name="email"></label>
            </div>
            <div>
                <label>Nachricht:<br>
                <textarea name="message" rows="4" cols="40"></textarea>
                </label>
            </div>
            <input type="submit" value="Absenden">
        </form>
    </div>
    
    <div class="result">
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
    </div>
    
    <h3>Test mit curl:</h3>
    <pre>curl -X POST -d "name=Testuser&email=test@example.com" http://localhost:8080/root/cgi-bin/post.rb</pre>
</body>
</html>
HTML