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
    let bottomVisible = false;

    const updateDirtyState = () => {
        bottomButton.disabled = !dirty;
        floating.classList.toggle("admin-save-floating-dirty", dirty);
        floating.classList.toggle(
            "admin-save-floating-hidden",
            !dirty || bottomVisible
        );
        label.textContent = "Ungespeicherte Änderungen";
    };

    const markDirty = () => {
        dirty = true;
        updateDirtyState();
    };

    form.addEventListener("input", markDirty);
    form.addEventListener("change", markDirty);

    form.addEventListener("submit", () => {
        dirty = false;
        updateDirtyState();
    });

    window.addEventListener("beforeunload", (event) => {
        if (!dirty) {
            return;
        }

        event.preventDefault();
        event.returnValue = "";
    });

    const observer = new IntersectionObserver((entries) => {
        bottomVisible = entries.some((entry) => entry.isIntersecting);
        updateDirtyState();
    }, {
        threshold: 0.15,
    });

    observer.observe(bottomButton);
    updateDirtyState();
})();
