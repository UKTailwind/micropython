import requests

# GitHub turns away a request that does not say who is asking
r = requests.get("https://api.github.com",
                 headers={"User-Agent": "pc3"})
print(r.status_code)               # 200 means "here you are"
print(r.text[:120])                # the reply is text...
# ALWAYS -- replies hold real memory
r.close()
