r = requests.get("https://api.open-meteo.com/v1/forecast"
                 "?latitude=51.5&longitude=-0.13"
                 "&current_weather=true")
data = r.json()
r.close()
print(data["current_weather"]["temperature"])
