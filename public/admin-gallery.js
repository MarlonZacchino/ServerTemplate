(function () {
    const list = document.querySelector('[data-gallery-sort-list]');
    const status = document.querySelector('[data-gallery-order-status]');

    if (!list) {
        return;
    }

    let activeItem = null;
    let pointerOwner = null;
    let activePointerId = null;
    let saveTimer = null;
    let orderBeforeMove = '';

    function setStatus(message, isError) {
        if (!status) {
            return;
        }

        status.textContent = message;
        status.classList.toggle('gallery-order-status-error', Boolean(isError));
    }

    function items() {
        return Array.from(list.querySelectorAll('[data-gallery-item]'));
    }

    function currentOrder() {
        return items()
            .map(function (item) {
                return item.dataset.imageId;
            })
            .filter(Boolean)
            .join(',');
    }

    function saveOrder() {
        const order = currentOrder();
        const csrfToken = list.dataset.csrfToken || '';

        if (!order || !csrfToken) {
            setStatus('Speichern nicht möglich', true);
            return;
        }

        setStatus('Speichert …', false);

        fetch('/admin/gallery/reorder', {
            method: 'POST',
            credentials: 'same-origin',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'
            },
            body: new URLSearchParams({
                csrf_token: csrfToken,
                order: order
            }).toString()
        }).then(function (response) {
            if (!response.ok) {
                throw new Error('Reihenfolge konnte nicht gespeichert werden.');
            }

            setStatus('Reihenfolge gespeichert', false);
        }).catch(function () {
            setStatus('Speichern fehlgeschlagen', true);
        });
    }

    function scheduleSave() {
        window.clearTimeout(saveTimer);
        saveTimer = window.setTimeout(saveOrder, 250);
    }

    function moveItemForPointer(clientY) {
        const otherItems = items().filter(function (item) {
            return item !== activeItem;
        });
        let insertBefore = null;

        otherItems.some(function (item) {
            const bounds = item.getBoundingClientRect();

            if (clientY < bounds.top + bounds.height / 2) {
                insertBefore = item;
                return true;
            }

            return false;
        });

        if (insertBefore) {
            list.insertBefore(activeItem, insertBefore);
        } else {
            list.appendChild(activeItem);
        }
    }

    function finishPointerSort(event) {
        if (!activeItem || event.pointerId !== activePointerId) {
            return;
        }

        if (pointerOwner && pointerOwner.hasPointerCapture(activePointerId)) {
            pointerOwner.releasePointerCapture(activePointerId);
        }

        activeItem.classList.remove('gallery-admin-item-dragging');
        document.body.classList.remove('gallery-sorting-active');

        const changed = currentOrder() !== orderBeforeMove;

        activeItem = null;
        pointerOwner = null;
        activePointerId = null;
        orderBeforeMove = '';

        if (changed) {
            scheduleSave();
        } else {
            setStatus('', false);
        }
    }

    list.querySelectorAll('[data-gallery-item]').forEach(function (item) {
        const handle = item.querySelector('.gallery-drag-handle');

        /*
         * Das native HTML-Drag-and-drop ist bei Bildern, Buttons und in
         * Firefox unzuverlässig. Deshalb verwenden wir Pointer Events.
         */
        item.draggable = false;

        if (handle) {
            handle.style.touchAction = 'none';
        }

        item.addEventListener('pointerdown', function (event) {
            const startedOnHandle = Boolean(event.target.closest('.gallery-drag-handle'));
            const startedOnControl = Boolean(
                event.target.closest('form, a, input, textarea, select, button')
            );

            if (event.button !== 0 || activeItem) {
                return;
            }

            /*
             * Mit der Maus kann die ganze Fotokarte gezogen werden.
             * Auf Touch-Geräten dient der Griff als eindeutige Ziehfläche,
             * damit normales Scrollen weiterhin möglich bleibt.
             */
            if ((event.pointerType === 'touch' && !startedOnHandle) ||
                (startedOnControl && !startedOnHandle)) {
                return;
            }

            event.preventDefault();
            activeItem = item;
            pointerOwner = item;
            activePointerId = event.pointerId;
            orderBeforeMove = currentOrder();

            item.setPointerCapture(event.pointerId);
            item.classList.add('gallery-admin-item-dragging');
            document.body.classList.add('gallery-sorting-active');
            setStatus('Reihenfolge ändern …', false);
        });

        item.addEventListener('pointermove', function (event) {
            if (!activeItem || event.pointerId !== activePointerId) {
                return;
            }

            event.preventDefault();
            moveItemForPointer(event.clientY);
        });

        item.addEventListener('pointerup', finishPointerSort);
        item.addEventListener('pointercancel', finishPointerSort);

        if (handle) {
            handle.addEventListener('keydown', function (event) {
                let sibling;

                if (event.key === 'ArrowUp') {
                    sibling = item.previousElementSibling;
                    if (sibling && sibling.matches('[data-gallery-item]')) {
                        event.preventDefault();
                        list.insertBefore(item, sibling);
                        scheduleSave();
                    }
                } else if (event.key === 'ArrowDown') {
                    sibling = item.nextElementSibling;
                    if (sibling && sibling.matches('[data-gallery-item]')) {
                        event.preventDefault();
                        list.insertBefore(sibling, item);
                        scheduleSave();
                    }
                }
            });
        }
    });
}());
