import datetime

now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
print("<html><body>")
print("<h1>Current Time</h1>")
print("<p>{}</p>".format(now))
print("</body></html>")
