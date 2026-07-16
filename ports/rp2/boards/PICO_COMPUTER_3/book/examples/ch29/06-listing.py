# mqtt_hello.py -- say hello on a public test broker.
# (Public brokers are for hellos, never for anything private!)
import time
from umqtt.simple import MQTTClient

wifi()
ME = "pc3-ada"                     # every client needs a UNIQUE name

c = MQTTClient(ME, "test.mosquitto.org")
c.connect()

def on_msg(topic, msg):
    print("heard:", msg.decode())

c.set_callback(on_msg)
c.subscribe(b"pc3/book/chat")
c.publish(b"pc3/book/chat", b"hello from " + ME)

for _ in range(300):               # listen for ~30 seconds
    c.check_msg()                  # deliver anything that arrived
    time.sleep_ms(100)
c.disconnect()
print("done listening")
