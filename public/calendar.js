(() => {
    "use strict";

    const form = document.querySelector("[data-booking-form]");
    const calendar = document.querySelector("[data-booking-calendar]");

    if (!form || !calendar) {
        return;
    }

    const serviceSelect = form.querySelector("#service");
    const appointmentDate = form.querySelector("#appointment-date");
    const appointmentStart = form.querySelector("#appointment-start");
    const submitButton = form.querySelector("[data-booking-submit]");
    const monthTitle = calendar.querySelector("[data-calendar-month]");
    const daysContainer = calendar.querySelector("[data-calendar-days]");
    const slotsContainer = calendar.querySelector("[data-calendar-slots]");
    const status = calendar.querySelector("[data-calendar-status]");
    const previousButton = calendar.querySelector("[data-calendar-previous]");
    const nextButton = calendar.querySelector("[data-calendar-next]");
    const selectionText = calendar.querySelector("[data-calendar-selection]");

    const today = new Date();
    let currentMonth = new Date(Date.UTC(today.getFullYear(), today.getMonth(), 1));
    let maximumMonth = new Date(Date.UTC(today.getFullYear(), today.getMonth() + 3, 1));
    let displayedMonth = new Date(currentMonth);
    let daysByDate = new Map();
    let selectedDay = "";
    let requestController = null;

    const formatDate = (date) => {
        const year = date.getUTCFullYear();
        const month = String(date.getUTCMonth() + 1).padStart(2, "0");
        const day = String(date.getUTCDate()).padStart(2, "0");
        return `${year}-${month}-${day}`;
    };

    const parseDate = (value) => {
        const [year, month, day] = value.split("-").map(Number);
        return new Date(Date.UTC(year, month - 1, day));
    };

    const formatDisplayDate = (value) => new Intl.DateTimeFormat("de-DE", {
        weekday: "long",
        day: "2-digit",
        month: "long",
        year: "numeric",
        timeZone: "UTC",
    }).format(parseDate(value));

    const setStatus = (message, error = false) => {
        status.textContent = message;
        status.classList.toggle("calendar-status-error", error);
    };

    const clearSelection = () => {
        selectedDay = "";
        appointmentDate.value = "";
        appointmentStart.value = "";
        submitButton.disabled = true;
        selectionText.textContent = "Noch kein Termin ausgewählt.";
        slotsContainer.replaceChildren();
    };

    const createSlotButton = (slot, date) => {
        const button = document.createElement("button");
        button.type = "button";
        button.className = "calendar-slot";
        button.disabled = !slot.available;
        button.textContent = slot.available
            ? `${slot.start}–${slot.end} Uhr`
            : `${slot.start}–${slot.end} Uhr · belegt`;

        if (slot.available) {
            button.addEventListener("click", () => {
                slotsContainer.querySelectorAll(".calendar-slot-selected").forEach((element) => {
                    element.classList.remove("calendar-slot-selected");
                    element.setAttribute("aria-pressed", "false");
                });

                button.classList.add("calendar-slot-selected");
                button.setAttribute("aria-pressed", "true");
                appointmentDate.value = date;
                appointmentStart.value = slot.start;
                submitButton.disabled = false;
                selectionText.textContent = `${formatDisplayDate(date)}, ${slot.start}–${slot.end} Uhr`;
            });
            button.setAttribute("aria-pressed", "false");
        }

        return button;
    };

    const renderSlots = (date) => {
        const day = daysByDate.get(date);
        slotsContainer.replaceChildren();
        appointmentDate.value = "";
        appointmentStart.value = "";
        submitButton.disabled = true;
        selectionText.textContent = `${formatDisplayDate(date)} ausgewählt. Bitte Uhrzeit wählen.`;

        if (!day || day.slots.length === 0) {
            const message = document.createElement("p");
            message.textContent = "An diesem Tag gibt es keine buchbaren Zeiten.";
            slotsContainer.append(message);
            return;
        }

        day.slots.forEach((slot) => slotsContainer.append(createSlotButton(slot, date)));
    };

    const renderDays = () => {
        daysContainer.replaceChildren();
        const year = displayedMonth.getUTCFullYear();
        const month = displayedMonth.getUTCMonth();
        const firstWeekday = (displayedMonth.getUTCDay() + 6) % 7;
        const daysInMonth = new Date(Date.UTC(year, month + 1, 0)).getUTCDate();

        for (let index = 0; index < firstWeekday; index += 1) {
            const spacer = document.createElement("span");
            spacer.className = "calendar-day-spacer";
            spacer.setAttribute("aria-hidden", "true");
            daysContainer.append(spacer);
        }

        for (let dayNumber = 1; dayNumber <= daysInMonth; dayNumber += 1) {
            const date = formatDate(new Date(Date.UTC(year, month, dayNumber)));
            const day = daysByDate.get(date);
            const hasAvailableSlot = day?.slots.some((slot) => slot.available) ?? false;
            const button = document.createElement("button");

            button.type = "button";
            button.className = "calendar-day";
            button.textContent = String(dayNumber);
            button.disabled = !hasAvailableSlot;
            button.setAttribute(
                "aria-label",
                hasAvailableSlot
                    ? `${formatDisplayDate(date)} – freie Termine vorhanden`
                    : `${formatDisplayDate(date)} – keine freien Termine`,
            );

            if (hasAvailableSlot) {
                button.classList.add("calendar-day-available");
                button.addEventListener("click", () => {
                    daysContainer.querySelectorAll(".calendar-day-selected").forEach((element) => {
                        element.classList.remove("calendar-day-selected");
                    });
                    button.classList.add("calendar-day-selected");
                    selectedDay = date;
                    renderSlots(date);
                });
            }

            daysContainer.append(button);
        }
    };

    const loadMonth = async () => {
        clearSelection();
        daysByDate = new Map();
        monthTitle.textContent = new Intl.DateTimeFormat("de-DE", {
            month: "long",
            year: "numeric",
            timeZone: "UTC",
        }).format(displayedMonth);
        previousButton.disabled = displayedMonth <= currentMonth;
        nextButton.disabled = displayedMonth >= maximumMonth;

        if (!serviceSelect.value) {
            renderDays();
            setStatus("Bitte zuerst eine Leistung auswählen.");
            return;
        }

        requestController?.abort();
        requestController = new AbortController();
        setStatus("Freie Termine werden geladen …");

        const firstDay = new Date(displayedMonth);
        const lastDay = new Date(Date.UTC(
            displayedMonth.getUTCFullYear(),
            displayedMonth.getUTCMonth() + 1,
            0,
        ));
        const query = new URLSearchParams({
            service: serviceSelect.value,
            from: formatDate(firstDay),
            to: formatDate(lastDay),
        });

        try {
            const response = await fetch(`/api/availability?${query}`, {
                headers: {Accept: "application/json"},
                cache: "no-store",
                signal: requestController.signal,
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }

            const data = await response.json();
            daysByDate = new Map(data.days.map((day) => [day.date, day]));
            renderDays();

            const availableDays = data.days.filter((day) =>
                day.slots.some((slot) => slot.available),
            ).length;

            setStatus(
                availableDays > 0
                    ? `${availableDays} Tag${availableDays === 1 ? "" : "e"} mit freien Terminen.`
                    : "In diesem Monat sind für diese Leistung keine freien Termine vorhanden.",
            );
        } catch (error) {
            if (error.name === "AbortError") {
                return;
            }
            renderDays();
            setStatus("Die freien Termine konnten nicht geladen werden. Bitte später erneut versuchen.", true);
        }
    };

    const loadServices = async () => {
        serviceSelect.disabled = true;
        submitButton.disabled = true;

        try {
            const response = await fetch("/api/services", {
                headers: {Accept: "application/json"},
                cache: "no-store",
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }

            const data = await response.json();
            const serverDate = parseDate(data.current_date);
            const horizonDate = new Date(serverDate);
            horizonDate.setUTCDate(horizonDate.getUTCDate() + data.booking_horizon_days);
            currentMonth = new Date(Date.UTC(
                serverDate.getUTCFullYear(),
                serverDate.getUTCMonth(),
                1,
            ));
            maximumMonth = new Date(Date.UTC(
                horizonDate.getUTCFullYear(),
                horizonDate.getUTCMonth(),
                1,
            ));
            displayedMonth = new Date(currentMonth);
            serviceSelect.replaceChildren();

            const placeholder = document.createElement("option");
            placeholder.value = "";
            placeholder.textContent = "Bitte auswählen";
            serviceSelect.append(placeholder);

            data.services.forEach((service) => {
                const option = document.createElement("option");
                option.value = service.code;
                option.textContent = `${service.name} · ca. ${service.duration_minutes} Minuten`;
                serviceSelect.append(option);
            });

            serviceSelect.disabled = false;
            setStatus("Bitte eine Leistung auswählen.");
            renderDays();
        } catch (error) {
            serviceSelect.replaceChildren();
            const option = document.createElement("option");
            option.value = "";
            option.textContent = "Leistungen momentan nicht verfügbar";
            serviceSelect.append(option);
            setStatus("Der Terminkalender ist momentan nicht erreichbar.", true);
        }
    };

    serviceSelect.addEventListener("change", loadMonth);
    previousButton.addEventListener("click", () => {
        displayedMonth = new Date(Date.UTC(
            displayedMonth.getUTCFullYear(),
            displayedMonth.getUTCMonth() - 1,
            1,
        ));
        loadMonth();
    });
    nextButton.addEventListener("click", () => {
        if (displayedMonth >= maximumMonth) {
            return;
        }
        displayedMonth = new Date(Date.UTC(
            displayedMonth.getUTCFullYear(),
            displayedMonth.getUTCMonth() + 1,
            1,
        ));
        loadMonth();
    });

    form.addEventListener("submit", (event) => {
        if (!appointmentDate.value || !appointmentStart.value) {
            event.preventDefault();
            setStatus("Bitte wähle zuerst einen freien Termin aus.", true);
            calendar.scrollIntoView({behavior: "smooth", block: "start"});
        }
    });

    loadServices();
})();
