import random
from time import sleep

from flask import Flask, jsonify, request
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

signals = [
    {"protocol": "COOLIX", "code": "0xB23F60", "description": "Power: on. Mode: Cool. Temp: 21c. Fans: Maximum", "success": True},
    {"protocol": "COOLIX", "code": "0xB27BE0", "description": "Power: off", "success": True},
    {"protocol": "COOLIX", "code": "0xB20FE0", "description": "Swing", "success": True},
]


@app.route('/<id>/')
def index(_id):
    return jsonify("IR Receiver Device")


@app.route('/<id>/status')
def status(_id=0):
    _id = int(_id)
    sleep(1)
    caps = ["SEND_SIGNAL", "READ_SIGNAL"] if (_id % 2) == 0 else ["SEND_SIGNAL"] if (_id % 3) == 0 else ["READ_SIGNAL"]
    bat = "100.00" if (_id % 2) == 0 else "50.00"
    return jsonify({"capabilities": caps, "battery": bat})


@app.route('/<id>/read')
def read_signal(_id=0):
    print("read signal")
    sleep(1)
    return jsonify(random.choice(signals))


@app.route('/<id>/send', methods=["POST"])
def send_signal(_id=0):
    sig = request.args.get("signal")
    protocol = request.args.get("protocol")
    print(f"{_id} - emit signal: {sig} using protocol {protocol}")
    sleep(1)
    return jsonify("Success")


app.run(debug=False)
