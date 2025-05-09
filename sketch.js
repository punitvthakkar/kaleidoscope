// Global variables
let symmetry = 6; // Number of sections in the kaleidoscope
let angle; // Angle of each section

let penColor;
let backgroundColor;
let strokeWidth;

// Variables to control color and stroke changes with mouse
let hueValue = 0;
let lastMouseX = 0;
let lastMouseY = 0;

function setup() {
  createCanvas(windowWidth, windowHeight);
  angleMode(DEGREES); // Use degrees for rotation

  angle = 360 / symmetry;
  backgroundColor = color(20, 20, 30); // Dark background
  penColor = color(255, 200, 0, 150); // Initial pen color (with some transparency)
  strokeWidth = 4;

  background(backgroundColor);

  // Move the origin to the center of the canvas
  translate(width / 2, height / 2);

  // Instructions
  let instructions = createP(
    "Move your mouse to draw. <br>" +
    "Click to clear the canvas. <br>" +
    "Press 'S' to change symmetry (cycles 3-12).<br>" +
    "Press 'C' to cycle through color modes."
  );
  instructions.style('color', 'white');
  instructions.style('position', 'absolute');
  instructions.style('top', '10px');
  instructions.style('left', '10px');
  instructions.style('font-family', 'sans-serif');
  instructions.style('font-size', '14px');

  lastMouseX = mouseX;
  lastMouseY = mouseY;
}

let colorModeIndex = 0;
const colorModes = ['dynamicHue', 'mouseSpeedColor', 'staticColor'];

function draw() {
  // Translate to the center for every frame to ensure transformations are correct
  translate(width / 2, height / 2);

  // Only draw if the mouse is within the canvas boundaries
  if (mouseX > 0 && mouseX < width && mouseY > 0 && mouseY < height) {
    // Calculate mouse coordinates relative to the center
    let mx = mouseX - width / 2;
    let my = mouseY - height / 2;
    let pmx = pmouseX - width / 2;
    let pmy = pmouseY - height / 2;

    // Change color and stroke based on mouse speed or position
    let mouseSpeed = dist(mouseX, mouseY, pmouseX, pmouseY);

    if (colorModes[colorModeIndex] === 'dynamicHue') {
      hueValue = (hueValue + mouseSpeed / 2) % 360;
      penColor = color(hueValue, 80, 90, 150); // HSB mode: Hue, Saturation, Brightness, Alpha
      colorMode(HSB, 360, 100, 100, 255);
    } else if (colorModes[colorModeIndex] === 'mouseSpeedColor') {
      let r = map(mouseSpeed, 0, 30, 100, 255);
      let g = map(mx, -width/2, width/2, 50, 200);
      let b = map(my, -height/2, height/2, 100, 255);
      penColor = color(r, g, b, 150);
      colorMode(RGB, 255);
    } else { // staticColor or default
      penColor = color(255, 200, 0, 150); // Default bright color
      colorMode(RGB, 255);
    }

    strokeWidth = map(mouseSpeed, 0, 50, 2, 15, true); // Stroke weight changes with speed, constrained

    if (mouseIsPressed || true) { // Continuously draw or only on drag (change `true` to `mouseIsPressed`)
      for (let i = 0; i < symmetry; i++) {
        push(); // Save current state
        rotate(angle * i);

        // Draw the primary line/shape
        stroke(penColor);
        strokeWeight(strokeWidth);
        // You can change what is drawn here: line, ellipse, rect, custom shape etc.
        line(mx, my, pmx, pmy);
        // ellipse(mx, my, mouseSpeed + 5, mouseSpeed + 5); // Example: draw ellipses

        // Apply reflection (mirror image)
        push(); // Save state before scaling
        scale(1, -1); // Flip vertically
        line(mx, my, pmx, pmy);
        // ellipse(mx, my, mouseSpeed + 5, mouseSpeed + 5); // Mirrored ellipse
        pop(); // Restore state after scaling

        /*
        // Optional: Add another reflection for more complex patterns
        push();
        scale(-1, 1); // Flip horizontally
        line(mx, my, pmx, pmy);
        pop();

        push();
        scale(-1,-1); // Flip both
        line(mx,my,pmx,pmy);
        pop();
        */

        pop(); // Restore state for next segment
      }
    }
  }
  // Reset colorMode to RGB for background and UI elements if HSB was used
  colorMode(RGB, 255);
  lastMouseX = mouseX;
  lastMouseY = mouseY;
}

function mousePressed() {
  // Clear the canvas when mouse is clicked
  // (Ensure click is not on UI elements if you add them)
  background(backgroundColor);
}

function keyPressed() {
  if (key === 's' || key === 'S') {
    symmetry++;
    if (symmetry > 12) {
      symmetry = 3; // Cycle between 3 and 12 segments
    }
    angle = 360 / symmetry;
    background(backgroundColor); // Clear canvas on symmetry change
    print("Symmetry changed to: " + symmetry);
  } else if (key === 'c' || key === 'C') {
    colorModeIndex = (colorModeIndex + 1) % colorModes.length;
    background(backgroundColor);
    print("Color mode changed to: " + colorModes[colorModeIndex]);
  }
}

// Adjust canvas size when window is resized
function windowResized() {
  resizeCanvas(windowWidth, windowHeight);
  background(backgroundColor);
  translate(width / 2, height / 2); // Re-center origin
}
