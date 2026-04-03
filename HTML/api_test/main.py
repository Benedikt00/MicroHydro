

from flask import Flask, jsonify, request

app = Flask(__name__)

# Sample data
items = [
    {"id": 1, "name": "Apple", "price": 0.99},
    {"id": 2, "name": "Banana", "price": 0.49},
    {"id": 3, "name": "Cherry", "price": 2.99},
]

new = False

@app.route("/api/get", methods=["GET"])
def get_items():
    """Return all items."""
    new = False
    return jsonify("0000000000000.000.00000000000001"), 200

import threading


def toggle_new():
    global new
    while True:
        threading.Event().wait(5)
        new = True

thread = threading.Thread(target=toggle_new, daemon=True)
thread.start()

@app.route("/api/get/new", methods=["GET"])
def get_items_new():
    """Return all items."""
    return jsonify(new), 200

@app.route("/api/post", methods=["POST"])
def create_item():
    """Create a new item."""
    data = request.get_json()
    print(data)

    return jsonify("0000000000000.000.00000000000001"), 201



if __name__ == "__main__":
    app.run(debug=True, host='0.0.0.0')








