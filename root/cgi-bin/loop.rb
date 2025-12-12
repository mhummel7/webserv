#!/usr/bin/env ruby

puts "Content-Type: text/html\r\n\r\n"

puts <<HTML
<!DOCTYPE html>
<html>
<head>
    <title>Endless Loop Test</title>
    <style>
        body { font-family: monospace; margin: 20px; }
        .loop { color: red; font-weight: bold; }
        .info { background: yellow; padding: 10px; }
    </style>
</head>
<body>
    <div class="info">
        <h1>⚠️ Endlosschleife Test</h1>
        <p>Dieses Skript wird in eine Endlosschleife gehen und muss vom Server gekillt werden.</p>
        <p>Startzeit: #{Time.now}</p>
        <p>PID: #{Process.pid}</p>
    </div>
    
    <h2>Ausgabe vor der Schleife:</h2>
    <p>Dies sollte sofort angezeigt werden...</p>
HTML

# Buffer leeren, damit die Ausgabe sofort erscheint
$stdout.flush

# Endlosschleife mit Ausgabe
counter = 0
begin
  loop do
    counter += 1
    puts "<p class='loop'>🌀 Schleifen-Iteration #{counter} - Zeit: #{Time.now}</p>"
    $stdout.flush  # Buffer leeren für sofortige Ausgabe
    
    # Verschiedene Möglichkeiten die Schleife "interessant" zu machen:
    
    # 1. CPU-intensive Berechnung
    # Math.sqrt(counter**3) * Math.log(counter + 1)
    
    # 2. Memory allokieren (wird mit der Zeit mehr)
    # array = Array.new(counter % 1000) { rand }
    
    # 3. Datei-Operation
    # File.write("/tmp/loop_#{Process.pid}.txt", counter.to_s) if counter % 10 == 0
    
    # 4. Sleep für sichtbare Ausgabe (optional)
    sleep 0.5 if counter < 20  # Nur für die ersten 20 Iterationen
    
    # 5. Nach 30 Iterationen wirklich endlos (ohne sleep)
    
    # Exit condition für Tests (kommentiert)
    # break if counter > 50
  end
rescue => e
  puts "<p style='color: red;'><strong>Unterbrochen:</strong> #{e.class}: #{e.message}</p>"
ensure
  puts "<h2>Ende (wird normalerweise nicht erreicht)</h2>"
  puts "<p>Schleife beendet nach #{counter} Iterationen</p>"
end

puts "</body></html>"