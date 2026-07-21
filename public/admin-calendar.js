"use strict";

(() => {
    const form = document.getElementById("calendar-settings-form");
    const floating = document.getElementById("calendar-save-floating");
    const bottomButton = document.getElementById("calendar-save-bottom");
    const label = document.getElementById("calendar-unsaved-label");

    if (!form || !floating || !bottomButton || !label) {
        return;
    }

    let dirty = false;

    const updateDirtyState = () => {
        floating.classList.toggle("admin-save-floating-dirty", dirty);
        label.textContent = dirty
            ? "Ungespeicherte Änderungen"
            : "Noch keine ungespeicherten Änderungen";
    };

    form.addEventListener("input", () => {
        dirty = true;
        updateDirtyState();
    });

    form.addEventListener("change", () => {
        dirty = true;
        updateDirtyState();
    });

    form.addEventListener("submit", () => {
        dirty = false;
        updateDirtyState();
    });

    const observer = new IntersectionObserver((entries) => {
        const bottomVisible = entries.some((entry) => entry.isIntersecting);
        floating.classList.toggle("admin-save-floating-hidden", bottomVisible);
    }, {
        threshold: 0.15,
    });

    observer.observe(bottomButton);
    updateDirtyState();
})();
