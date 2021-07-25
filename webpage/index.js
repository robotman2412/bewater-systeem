
// Holder for zone elements.
var zonesElem;
// Whether or not this is a mobile device.
let isMobile = false;
// A list of zones.
let zones = [
	{ index: 0, name: "Licht",   id: "ledinar", state: false },
	{ index: 1, name: "Zone #1", id: "zone1",   state: false },
	{ index: 2, name: "Zone #2", id: "zone2",   state: false },
	{ index: 3, name: "Zone #3", id: "zone3",   state: false },
	{ index: 4, name: "Zone #4", id: "zone4",   state: false },
	{ index: 5, name: "Pomp",    id: "pump",    state: false },
];
// Map from pin ID to zone index.
let idToZone = {};
// Description of schedule id.
let excToDesc = {
	none: "Geen",
	pin: "Interactie met schema",
	any: "Alle interactie",
};
// Timer for update.
var updateTimer;
// Used to check if states are identical.
var stateMagic;


// Called when the body is loaded.
function loaded() {
	// Generate some content.
	zonesElem = document.getElementById("zones");
	for (i in zones) {
		idToZone[zones[i].id] = i;
		zonesElem.innerHTML += '<div id="zone_'+i+'" class="zone" onclick="toggle(this, '+i+')"><div>'+zones[i].name+'</div></div>';
	}
	// Set the update thing.
	updateTimer = setInterval(update, 1000);
	update();
	// Do not update on blur.
	window.onblur = () => {
		clearInterval(updateTimer);
		updateTimer = null;
	}
	// Continue to update on focus.
	window.onfocus = () => {
		if (updateTimer) clearInterval(updateTimer);
		updateTimer = setInterval(update, 1000);
		update();
	};
}

// Update the style of elem to reflect state.
function updateStyle(elem, state) {
	elem.classList.remove("zone-pending-on");
	elem.classList.remove("zone-pending-off");
	elem.classList.remove("zone-on");
	elem.classList.remove("zone-off");
	elem.classList.add("zone-" + state);
}

// Send a request by URL.
function req(url, method) {
	var xhttp = new XMLHttpRequest();
	xhttp.open(method || 'GET', url);
	xhttp.send();
	return xhttp;
}

// Turn on a pin by ID.
function on(pin) {
	return req('/on/' + pin);
}

// Turn off a pin by ID.
function off(pin) {
	return req('/off/' + pin);
}

// Read the pin states.
function update() {
	var xhttp = req("/read");
	xhttp.onreadystatechange = () => {
		if (xhttp.readyState == 4 && xhttp.status == 200) {
			let resp = JSON.parse(xhttp.responseText);
			if (resp.magic == stateMagic) return;
			stateMagic = resp.magic;
			let pins = resp.pins;
			for (id in pins) {
				// Update the styles.
				let index = idToZone[id];
				let elem = document.getElementById("zone_" + index);
				let value = pins[id].indexOf("on") >= 0;
				zones[index].state = value;
				updateStyle(elem, pins[id]);
			}
		}
	};
	var xhttp1 = req("/sched_list");
	xhttp1.onreadystatechange = () => {
		if (xhttp1.readyState == 4 && xhttp1.status == 200) {
			let resp = JSON.parse(xhttp1.responseText);
			updatechedule(resp.schedule);
		}
	};
}

// Read the value of a zone by index.
function read(index) {
	var xhttp = req("/read/" + zones[index].id);
	xhttp.onreadystatechange = () => {
		if (xhttp.readyState == 4 && xhttp.status == 200) {
			let elem = document.getElementById("zone_" + index);
			let value = xhttp.responseText.indexOf("on") >= 0;
			zones[index].state = value;
			updateStyle(elem, xhttp.responseText);
		}
	};
}

// Toggle a zone by index.
function toggle(elem, index) {
	let zone = zones[index];
	let to = !zone.state;
	zone.state = !zone.state;
	// To read the state and not with the timer.
	var xhttp;
	let onready = () => {
		if (xhttp.readyState == 4) {
			let resp = xhttp.responseText;
			if (resp == "write_error") erreur(elem, zone.name + " kan nu niet " + (to ? "aan" : "uit"));
			read(index);
		}
	}
	// Send a command.
	if (zone.state) {
		updateStyle(elem, "pending-on");
		xhttp = on(zone.id);
		xhttp.onreadystatechange = onready;
	} else {
		updateStyle(elem, "pending-off");
		xhttp = off(zone.id);
		xhttp.onreadystatechange = onready;
	}
}

// Play the error animation.
function erreur(elem, msg) {
	if (elem) {
		elem.classList.add("zone-error");
		setTimeout(() => {
			elem.classList.remove("zone-error");
		}, 500);
	}
	if (msg) {
		console.log("ERREUR:", msg);
	}
}

// Describe milliseconds as text.
function desctime(time) {
	if (time < 0) {
		return "0";
	}
	var num, unit;
	if (time < 60000) {
		num = time / 1000;
		unit = 's';
	} else if (time < 3600000) {
		num = time / 60000;
		unit = 'm';
	} else {
		num = time / 3600000;
		unit = 'h';
	}
	return Math.round(num) + unit;
}

// Update the schedule elements.
function updatechedule(schedule) {
	let now = new Date().getTime();
	for (i in schedule) {
		var sched = schedule[i];
		var except = sched.except == "none" ? "" : "Uitzondering: " + excToDesc[sched.except]
		var raw = '<div id="schedule_'+sched.id+'" class="zone zone-on"><div>' +
			'<table style="padding: 0; border-spacing: 0; width: 100%;">' +
			'<td id="schedule_'+sched.id+'_desc" style="width:33.33%"                  >'+sched.desc+'</td>' +
			'<td id="schedule_'+sched.id+'_exc"  style="width:33.33%;text-align:center">'+except+'</td>' +
			'<td id="schedule_'+sched.id+'_time" style="width:33.33%;text-align:right" >'+desctime(sched.time-now)+'</td>' +
			'</table></div></div>';
	}
}

// Wow!
function debugschedule(schedule) {
	let now = new Date().getTime();
	for (i in schedule) {
		var sched = schedule[i];
		var raw = sched.desc + '    Uitzondering: ' + excToDesc[sched.except] + '    ' + desctime(sched.time - now);
		console.log(raw);
	}
}
