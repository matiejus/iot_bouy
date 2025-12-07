// db.js
const mysql = require("mysql2/promise");
const fs = require("fs");
const path = require("path");

// Read credentials from logins.txt
const loginsPath = path.join(__dirname, "logins.txt");
const loginsContent = fs.readFileSync(loginsPath, "utf8");

const credentials = {};
loginsContent.split("\n").forEach(line => {
    const [key, value] = line.split("=");
    if (key && value) {
        credentials[key.trim()] = value.trim();
    }
});

const pool = mysql.createPool({
    host: credentials.host,
    user: credentials.user,
    password: credentials.password,
    database: credentials.database,
});

module.exports = pool;
