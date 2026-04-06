const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()_+-=[]{}|;:,.<>?/';
const textEl = document.getElementById('status-text');
const originalText = 'COMING SOON';

function createEncryptEffect() {
  textEl.innerHTML = '';
  const charSpans = originalText.split('').map((char, index) => {
    const span = document.createElement('span');
    span.className = char === ' ' ? 'char space' : 'char';
    span.textContent = char;
    textEl.appendChild(span);
    return { span, originalChar: char, index };
  });

  charSpans.forEach(({ span, originalChar, index }) => {
    const delay = index * 40;
    const duration = 400 + Math.random() * 200;

    setTimeout(() => {
      let elapsed = 0;
      const interval = 30;

      const cycleInterval = setInterval(() => {
        elapsed += interval;

        if (elapsed < duration) {
          const randomChar = originalChar === ' '
            ? ' '
            : chars[Math.floor(Math.random() * chars.length)];
          span.textContent = randomChar;
          span.classList.add('encrypt');
        } else {
          span.textContent = originalChar;
          span.classList.remove('encrypt');
          clearInterval(cycleInterval);
        }
      }, interval);
    }, delay);
  });
}

function runLoop() {
  createEncryptEffect();
  setTimeout(runLoop, 2800);
}

runLoop();
