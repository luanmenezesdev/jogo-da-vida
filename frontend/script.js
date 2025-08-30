const oled = document.getElementById("oled");
const pixels = [];
const activeCoords = new Set();
let isDrawing = false;

// Criar grid 128x64 com pixels
for (let y = 0; y < 64; y++) {
  for (let x = 0; x < 128; x++) {
    const div = document.createElement("div");
    div.classList.add("pixel");
    div.dataset.x = x;
    div.dataset.y = y;

    const togglePixel = () => {
      div.classList.toggle("active");
      const key = `${x},${y}`;
      if (div.classList.contains("active")) activeCoords.add(key);
      else activeCoords.delete(key);
    };

    div.addEventListener("mousedown", () => {
      isDrawing = true;
      togglePixel();
    });
    div.addEventListener("mouseenter", () => {
      if (isDrawing) togglePixel();
    });
    div.addEventListener("mouseup", () => {
      isDrawing = false;
    });

    oled.appendChild(div);
    pixels.push(div);
  }
}

document.addEventListener("mouseup", () => {
  isDrawing = false;
});

// Criar labels de linhas e colunas (estilo Batalha Naval)
const rowLabels = document.getElementById("row-labels");
for (let y = 0; y < 64; y++) {
  const div = document.createElement("div");
  div.style.height = "12px";
  div.textContent = y;
  rowLabels.appendChild(div);
}

const colLabels = document.getElementById("col-labels");
for (let x = 0; x < 128; x++) {
  const div = document.createElement("div");
  div.style.width = "12px";
  div.style.textAlign = "center";
  div.textContent = x % 10; // números repetindo de 0-9
  colLabels.appendChild(div);
}

// Botão limpar grid
document.getElementById("clearBtn").addEventListener("click", () => {
  pixels.forEach((p) => p.classList.remove("active"));
  activeCoords.clear();
});

// Conectar MQTT
const client = mqtt.connect("wss://broker.hivemq.com:8884/mqtt");
client.on("connect", () => {
  console.log("Conectado ao MQTT");
});

// Botão enviar para Pico
document.getElementById("sendBtn").addEventListener("click", () => {
  const coordsArray = Array.from(activeCoords).map((s) =>
    s.split(",").map(Number)
  );
  const message = JSON.stringify(coordsArray);
  client.publish("pico/life", message);
  console.log("Mensagem enviada:", message);
});
