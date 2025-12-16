# Zeigt: Server läuft, korrekte Headers, Keep-Alive, Content-Type
curl -v --http1.1 http://localhost:8080/

# Post a file in /data 
curl -v -X POST \
     -F "file=@test.txt" \
     http://localhost:8080/root/data/

# directory listing in data 
curl -v http://localhost:8080/root/data/                

# Testet Body-Parsing und Error 413 (wenn konfiguriert)
curl -v -X POST \
     -H "Content-Type: application/x-www-form-urlencoded" \
     -d "action=test&data=hello_world" \
     http://localhost:8080/root/data/

# CGI echo.cgi POST test
curl -v -X POST \
     -H "Content-Type: text/plain" \
     -d "Hello CGI World! This is a test from the evaluator. $(date)" \
     "http://localhost:8080/cgi-bin/echo.cgi"

# CGI GET 
curl -v "http://localhost:8080/root/cgi-bin/time.py"