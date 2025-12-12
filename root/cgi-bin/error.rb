#!/usr/bin/env ruby

puts "Content-Type: text/html\r\n\r\n"

puts <<HTML
<!DOCTYPE html>
<html>
<head>
    <title>CGI Error Test</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; }
        .error { color: red; font-weight: bold; background: #ffeeee; padding: 20px; border: 2px solid red; }
        .success { color: green; background: #eeffee; padding: 20px; border: 2px solid green; }
    </style>
</head>
<body>
    <h1>CGI Error Demonstration</h1>
    <p>Dieses Skript demonstriert einen einfachen Runtime-Fehler in Ruby.</p>
HTML

# Einfache Division durch Null - wird einen Fehler verursachen
begin
    puts "<div class='error'>"
    puts "<h2>Versuche Division durch Null:</h2>"
    
    zero = 0
    result = 100 / zero  # Hier kommt der Fehler!
    
    puts "<p>Ergebnis: #{result}</p>"
    puts "<p>Dies sollte nie erreicht werden.</p>"
    puts "</div>"
rescue => e
    puts "<div class='error'>"
    puts "<h2>❌ Fehler aufgetreten!</h2>"
    puts "<p><strong>Fehlertyp:</strong> #{e.class}</p>"
    puts "<p><strong>Fehlermeldung:</strong> #{e.message}</p>"
    puts "</div>"
end

puts <<HTML
    <hr>
    <div class='success'>
        <h2>✅ Skript-Ausführung abgeschlossen</h2>
        <p>Das Skript wurde trotz des Fehlers korrekt beendet.</p>
        <p>Zeit: #{Time.now}</p>
    </div>
</body>
</html>
HTML