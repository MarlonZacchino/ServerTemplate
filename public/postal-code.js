(() => {
    const postalCodeInput = document.querySelector('[data-postal-code]');
    const cityInput = document.querySelector('[data-city]');
    const suggestions = document.querySelector('[data-city-suggestions]');
    const status = document.querySelector('[data-postal-lookup-status]');

    if (!postalCodeInput || !cityInput || !suggestions) {
        return;
    }

    let requestController = null;
    let lookupTimer = null;
    let autoFilledCity = '';
    let lastRequestedPostalCode = '';

    const setStatus = (message, isError = false) => {
        if (!status) {
            return;
        }

        status.textContent = message;
        status.classList.toggle('postal-lookup-status-error', isError);
    };

    const clearSuggestions = () => {
        suggestions.replaceChildren();
    };

    const uniqueLocalities = (items) => {
        const names = new Set();

        if (!Array.isArray(items)) {
            return [];
        }

        items.forEach((item) => {
            const name = typeof item?.name === 'string' ? item.name.trim() : '';
            if (name) {
                names.add(name);
            }
        });

        return Array.from(names).sort((left, right) =>
            left.localeCompare(right, 'de-DE'),
        );
    };

    const applyLocalities = (postalCode, localities) => {
        if (postalCodeInput.value !== postalCode) {
            return;
        }

        clearSuggestions();
        localities.forEach((locality) => {
            const option = document.createElement('option');
            option.value = locality;
            suggestions.append(option);
        });

        if (localities.length === 0) {
            setStatus('Kein Ort gefunden. Bitte den Wohnort selbst eintragen.', false);
            return;
        }

        if (localities.length === 1) {
            if (!cityInput.value || cityInput.value === autoFilledCity) {
                cityInput.value = localities[0];
                autoFilledCity = localities[0];
            }
            setStatus('Wohnort wurde anhand der Postleitzahl ergänzt.', false);
            return;
        }

        if (!cityInput.value || cityInput.value === autoFilledCity) {
            cityInput.value = '';
            autoFilledCity = '';
        }
        setStatus('Mehrere Orte gefunden. Bitte den passenden Wohnort auswählen.', false);
    };

    const lookupPostalCode = async (postalCode) => {
        requestController?.abort();
        requestController = new AbortController();
        lastRequestedPostalCode = postalCode;
        setStatus('Wohnort wird gesucht …', false);

        try {
            const query = new URLSearchParams({postal_code: postalCode});
            const response = await fetch(`/api/postal-code?${query}`, {
                headers: {Accept: 'application/json'},
                cache: 'default',
                signal: requestController.signal,
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }

            const data = await response.json();
            applyLocalities(postalCode, uniqueLocalities(data));
        } catch (error) {
            if (error.name === 'AbortError') {
                return;
            }

            if (postalCodeInput.value === postalCode) {
                clearSuggestions();
                setStatus(
                    'Die automatische Ortssuche ist gerade nicht verfügbar. Bitte den Wohnort selbst eintragen.',
                    true,
                );
            }
        }
    };

    postalCodeInput.addEventListener('input', () => {
        window.clearTimeout(lookupTimer);
        requestController?.abort();
        clearSuggestions();

        const postalCode = postalCodeInput.value.trim();

        if (cityInput.value === autoFilledCity) {
            cityInput.value = '';
        }
        autoFilledCity = '';

        if (!/^\d{5}$/.test(postalCode)) {
            lastRequestedPostalCode = '';
            setStatus(
                postalCode.length === 0
                    ? 'Nach Eingabe der PLZ kann der Wohnort automatisch ergänzt werden.'
                    : 'Bitte eine fünfstellige Postleitzahl eingeben.',
                false,
            );
            return;
        }

        if (postalCode === lastRequestedPostalCode) {
            return;
        }

        lookupTimer = window.setTimeout(() => {
            lookupPostalCode(postalCode);
        }, 250);
    });

    cityInput.addEventListener('input', () => {
        if (cityInput.value !== autoFilledCity) {
            autoFilledCity = '';
        }
    });
})();
