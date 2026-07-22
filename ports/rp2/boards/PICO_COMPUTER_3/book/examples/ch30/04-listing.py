import requests

r = requests.get("https://api.github.com")
print(r.status_code)               # 200 means "here you are"
print(r.text[:120])                # the reply is text...
# ALWAYS -- replies hold real memory
r.close()
