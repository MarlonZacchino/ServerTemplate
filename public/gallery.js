(function () {
    const grid = document.querySelector('[data-gallery-grid]');
    const empty = document.querySelector('[data-gallery-empty]');

    if (!grid) {
        return;
    }

    const lightbox = document.createElement('dialog');
    lightbox.className = 'gallery-lightbox';
    lightbox.innerHTML = '<button class="gallery-lightbox-close" type="button" aria-label="Großansicht schließen">×</button><img alt="">';
    document.body.appendChild(lightbox);

    const lightboxImage = lightbox.querySelector('img');
    const closeButton = lightbox.querySelector('button');

    function closeLightbox() {
        if (lightbox.open) {
            lightbox.close();
        }
    }

    closeButton.addEventListener('click', closeLightbox);
    lightbox.addEventListener('click', function (event) {
        if (event.target === lightbox) {
            closeLightbox();
        }
    });

    function openLightbox(item) {
        lightboxImage.src = item.url;
        lightboxImage.alt = item.alt || item.title || 'Galeriebild';
        lightbox.showModal();
    }

    function createCard(item) {
        const article = document.createElement('article');
        article.className = 'gallery-card';

        const button = document.createElement('button');
        button.className = 'gallery-image-button';
        button.type = 'button';
        button.setAttribute('aria-label', 'Bild vergrößern');
        button.addEventListener('click', function () { openLightbox(item); });

        const image = document.createElement('img');
        image.className = 'gallery-image';
        image.src = item.url;
        image.alt = item.alt || item.title || 'Galeriebild';
        image.loading = 'lazy';

        const content = document.createElement('div');
        content.className = 'gallery-card-content';

        const title = document.createElement('h2');
        title.textContent = item.title || 'Styling 4 Dogs';

        button.appendChild(image);
        content.appendChild(title);
        article.appendChild(button);
        article.appendChild(content);
        return article;
    }

    fetch('/api/gallery', {headers: {'Accept': 'application/json'}})
        .then(function (response) {
            if (!response.ok) {
                throw new Error('Die Galerie konnte nicht geladen werden.');
            }
            return response.json();
        })
        .then(function (items) {
            grid.innerHTML = '';
            if (!Array.isArray(items) || items.length === 0) {
                if (empty) {
                    empty.hidden = false;
                }
                return;
            }
            items.forEach(function (item) {
                grid.appendChild(createCard(item));
            });
        })
        .catch(function () {
            if (empty) {
                empty.hidden = false;
                empty.textContent = 'Die Galerie konnte gerade nicht geladen werden.';
            }
        });
}());
