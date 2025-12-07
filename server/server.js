const express = require("express");
const path = require("path");
const mqtt = require("mqtt");
const db = require("./db");

const app = express();
const PORT = 3000;

// Serve frontend files
app.use(express.static(path.join(__dirname, "public")));

app.get("/", (req, res) => {
    res.sendFile(path.join(__dirname, "public", "index.html"));
});

app.get("/history", (req, res) => {
    res.sendFile(path.join(__dirname, "public", "history.html"));
});


//
// ===== LIVE DATA ENDPOINT =====
// Returns ONLY the newest measurement
//
app.get("/api/live/latest", async (req, res) => {
    try {
        const [rows] = await db.query(
            "SELECT * FROM sensor_readings ORDER BY id DESC LIMIT 1"
        );
        res.json(rows[0] || {});
    } catch (err) {
        res.status(500).json({ error: "DB error", details: err });
    }
});

//
// ===== HISTORY: RANGE QUERY =====
// /api/history?from=YYYY-MM-DD HH:MM:SS&to=YYYY-MM-DD HH:MM:SS
//
app.get("/api/history", async (req, res) => {
    let { from, to } = req.query;

    if (!from || !to) {
        return res.status(400).json({ error: "Missing from/to parameters" });
    }

    // Convert datetime-local to MySQL DATETIME
    from = from.replace("T", " ") + ":00";
    to = to.replace("T", " ") + ":59";

    try {
        const [rows] = await db.query(
            "SELECT * FROM sensor_readings WHERE timestamp BETWEEN ? AND ? ORDER BY timestamp ASC",
            [from, to]
        );
        res.json(rows);
    } catch (err) {
        res.status(500).json({ error: "DB error", details: err });
    }
});


//
// ===== HISTORY: LAST 50 =====
// This preserves your original behaviour
//
app.get("/api/history/latest50", async (req, res) => {
    try {
        const [rows] = await db.query(
            "SELECT * FROM sensor_readings ORDER BY id DESC LIMIT 50"
        );
        res.json(rows.reverse());
    } catch (err) {
        res.status(500).json({ error: "DB error", details: err });
    }
});

// ===== COMMAND: publish beacon command to MCU =====
// POST /api/command/beacon
app.post("/api/command/beacon", (req, res) => {
    const COMMAND_TOPIC = "mcu/beacon_mode";
    const payload = JSON.stringify({ id: 4 });

    client.publish(COMMAND_TOPIC, payload, { qos: 1 }, (err) => {
        if (err) {
            console.error("Failed to publish command:", err);
            return res.status(500).json({ error: "MQTT publish failed", details: String(err) });
        }
        console.log("Published command to", COMMAND_TOPIC, payload);
        res.json({ status: "ok", command: "beacon" });
    });
});

//
// ===== MQTT INGESTION (unchanged, fully compatible) =====
//
const MQTT_BROKER = "mqtt://broker.hivemq.com";
const MQTT_TOPIC = "mcu/sensors_output";

const client = mqtt.connect(MQTT_BROKER);

client.on("connect", () => {
    console.log("MQTT connected");
    client.subscribe(MQTT_TOPIC, () => {
        console.log("Subscribed to:", MQTT_TOPIC);
    });
});

client.on("message", async (topic, message) => {
    try {
        const json = JSON.parse(message.toString());
        console.log("Received:", json);

        const sql = `
            INSERT INTO sensor_readings
            (device_id, tds_raw, tds_volt, turb_raw, turb_volt, temp_c)
            VALUES (?, ?, ?, ?, ?, ?)
        `;

        await db.query(sql, [
            json.device_id ?? null,
            json.tds_raw ?? null,
            json.tds_volt ?? null,
            json.turb_raw ?? null,
            json.turb_volt ?? null,
            json.temp_c ?? null
        ]);

        console.log("Saved to DB");

    } catch (err) {
        console.error("Failed to process message:", err);
        console.log("Raw:", message.toString());
    }
});

//
// ===== START WEB SERVER =====
//
app.listen(PORT, () => {
    console.log(`Web server running at http://localhost:${PORT}`);
});
