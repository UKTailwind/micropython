import requests

r = requests.get("https://api.github.com")
print(r.status_code)               # 200 means "here you are"
print(r.text[:120])                # the reply is text...
r.close()                          # ALWAYS -- replies hold real memory
