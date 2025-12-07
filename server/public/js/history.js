document.getElementById("load").onclick = async () => {
    let from = document.getElementById("from").value;
    let to = document.getElementById("to").value;

    if (!from || !to) {
        return alert("Select date range!");
    }

    // send raw datetime-local values; server will convert to MySQL DATETIME
    const url = `/api/history?from=${encodeURIComponent(from)}&to=${encodeURIComponent(to)}`;

    document.getElementById("status").innerText = "Loading...";

    try {
        const res = await fetch(url);
        const data = await res.json();

        if (!data || data.length === 0) {
            document.getElementById("status").innerText = "No data for this range";
            return;
        }

        document.getElementById("status").innerText = `Loaded ${data.length} records`;

        updateCharts(data);
    } catch (err) {
        console.error(err);
        document.getElementById("status").innerText = "Error fetching data";
    }
};

function updateCharts(data) {
    const labels = data.map(r => r.timestamp.replace("T"," ").replace(".000Z",""));

    chartTemp.data.labels = labels;
    chartTemp.data.datasets[0].data = data.map(r => r.temp_c);
    chartTemp.update();

    chartTDS.data.labels = labels;
    chartTDS.data.datasets[0].data = data.map(r => r.tds_raw);
    chartTDS.update();

    chartTurb.data.labels = labels;
    chartTurb.data.datasets[0].data = data.map(r => r.turb_raw);
    chartTurb.update();
}

const chartTemp = new Chart(document.getElementById("chartTemp"), {
    type: "line",
    data: { labels: [], datasets: [{ label: "Temperature (°C)", data: [], borderWidth: 2 }] },
    options: { scales: { x: { display: true }, y: { beginAtZero: false } } }
});

const chartTDS = new Chart(document.getElementById("chartTDS"), {
    type: "line",
    data: { labels: [], datasets: [{ label: "TDS Raw", data: [], borderWidth: 2 }] },
    options: { scales: { x: { display: true }, y: { beginAtZero: true } } }
});

const chartTurb = new Chart(document.getElementById("chartTurb"), {
    type: "line",
    data: { labels: [], datasets: [{ label: "Turbidity Raw", data: [], borderWidth: 2 }] },
    options: { scales: { x: { display: true }, y: { beginAtZero: true } } }
});
