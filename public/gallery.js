(function () {
    const grid = document.querySelector('[data-gallery-grid]');
    const empty = document.querySelector('[data-gallery-empty]');

    if (!grid) {
        return;
    }

    function createCard(item) {
        const article = document.createElement('article');
        article.className = 'gallery-card';

        const image = document.createElement('img');
        image.className = 'gallery-image';
        image.src = item.url;
        image.alt = item.alt || item.title || 'Galeriebild';
        image.loading = 'lazy';

        const content = document.createElement('div');
        content.className = 'gallery-card-content';

        const title = document.createElement('h2');
        title.textContent = item.title || 'Styling 4 Dogs';

        content.appendChild(title);
        article.appendChild(image);
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
