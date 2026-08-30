const features = [
  {
    index: '01',
    symbol: '∞',
    title: 'Больше аккаунтов',
    text: 'До 32 аккаунтов в одном клиенте с аккуратной фоновой работой и быстрым переключением.',
  },
  {
    index: '02',
    symbol: '◉',
    title: 'Удалённое остаётся',
    text: 'Ручные удаления можно сохранять локально: сообщение становится прозрачным и получает метку {DELETED}.',
  },
  {
    index: '03',
    symbol: '↺',
    title: 'История редакций',
    text: 'Предыдущие версии текста доступны из контекстного меню — без вмешательства в облако Telegram.',
  },
  {
    index: '04',
    symbol: '⌁',
    title: 'Секретные чаты на ПК',
    text: 'Текст, файлы, таймеры и локальная история — внутри привычного интерфейса обычного диалога.',
  },
  {
    index: '05',
    symbol: '⇣',
    title: 'Медиа заранее',
    text: 'Клиент может загрузить разрешённые медиа до открытия чата и отметить локальную копию рядом со временем.',
  },
  {
    index: '06',
    symbol: 'A4',
    title: 'Экспорт в PDF и HTML',
    text: 'Выгружайте диалог целиком или только сообщения выбранного автора в читаемом формате.',
  },
];

const faqs = [
  ['Это официальный Telegram?', 'Нет. ά‑Gram — независимый форк Telegram Desktop и Telegram для Android. Он использует сеть Telegram, но развивается отдельно.'],
  ['Сессии сохранятся после обновления?', 'При обновлении поверх существующей portable-папки текущий tdata сохраняется. Перед заменой сборки всё равно полезно сделать резервную копию.'],
  ['Что не сохраняется?', 'Самоуничтожающиеся сообщения, медиа одного просмотра и данные, которые устройство не успело получить, намеренно исключены.'],
  ['Где хранится локальная история?', 'В зашифрованном локальном хранилище соответствующего аккаунта. Она не синхронизируется с другими устройствами.'],
];

export default function Home() {
  return (
    <main>
      <header className="site-header">
        <a className="brand" href="#top" aria-label="ά‑Gram — на главную">
          <span className="brand-mark">ά</span>
          <span>Gram</span>
        </a>
        <nav aria-label="Главная навигация">
          <a href="#features">Возможности</a>
          <a href="#privacy">Приватность</a>
          <a href="#download">Скачать</a>
        </nav>
        <a className="header-cta" href="#download">Получить клиент</a>
      </header>

      <section className="hero" id="top">
        <div className="hero-art" aria-hidden="true" />
        <div className="hero-copy">
          <p className="eyebrow"><span>Независимый клиент Telegram</span></p>
          <h1>Связь без тесных<br />рамок.</h1>
          <p className="lede">
            ά‑Gram соединяет привычный Telegram с расширенным контролем:
            больше аккаунтов, локальная история и секретные чаты на компьютере.
          </p>
          <div className="hero-actions">
            <a className="button button-primary" href="#download">Скачать ά‑Gram <span>↓</span></a>
            <a className="text-link" href="#features">Изучить возможности <span>↘</span></a>
          </div>
        </div>

        <div className="platform-strip" aria-label="Доступные платформы">
          <span className="edition">Edition XV</span>
          <div className="platform"><span className="platform-dot" /><strong>Windows</strong><small>x64 · 7.1.2</small></div>
          <div className="platform"><span className="platform-dot" /><strong>Android</strong><small>arm64 · 12.10.1</small></div>
          <span className="scroll-note">Листайте, чтобы узнать больше <b>↓</b></span>
        </div>
      </section>

      <section className="manifesto" id="features">
        <div className="section-kicker"><span>01 / Свобода</span><i /></div>
        <div className="manifesto-main">
          <h2>Один клиент.<br />Все ваши аккаунты.</h2>
          <p>Никакой искусственной тесноты.</p>
        </div>
        <div className="manifesto-aside">
          <span className="large-alpha" aria-hidden="true">ά</span>
          <p>Знакомый интерфейс Telegram — без необходимости заново учиться переписке.</p>
        </div>
      </section>

      <section className="features-grid" aria-label="Возможности ά‑Gram">
        {features.map((feature) => (
          <article className="feature-card" key={feature.index}>
            <div className="feature-top"><span>{feature.index}</span><b>{feature.symbol}</b></div>
            <h3>{feature.title}</h3>
            <p>{feature.text}</p>
            <span className="feature-rule" />
          </article>
        ))}
      </section>

      <section className="memory-section" id="privacy">
        <div className="memory-copy">
          <div className="section-kicker light"><span>02 / Память</span><i /></div>
          <h2>Сообщение исчезло.<br /><em>Контекст — нет.</em></h2>
          <p>
            Включайте локальное сохранение отдельно для каждого аккаунта. ά‑Gram отмечает
            серверное удаление там же, где время, и не подменяет исходный текст.
          </p>
          <ul>
            <li>60% прозрачности для удалённых сообщений</li>
            <li>локальная история редактирований</li>
            <li>отдельная архивная копия медиа</li>
          </ul>
        </div>

        <div className="chat-stage" aria-label="Пример отображения удалённых сообщений">
          <div className="chat-window">
            <div className="chat-head">
              <span className="avatar">A</span>
              <div><strong>Alex</strong><small>был(а) недавно</small></div>
              <span className="chat-lock">⌕</span>
            </div>
            <div className="chat-body">
              <span className="day">31 августа</span>
              <div className="bubble incoming">Встречаемся в семь у старого театра?<small>18:42</small></div>
              <div className="bubble outgoing">Да. Я сохраню маршрут и напишу, когда буду рядом.<small>18:43 ✓✓</small></div>
              <div className="bubble incoming deleted">Возьми с собой тот файл из архива.<small><b>{'{DELETED}'}</b> 18:44</small></div>
              <div className="bubble media">
                <span className="media-art">α</span>
                <span>project-notes.pdf<small>2.8 МБ · PDF</small></span>
                <small className="media-time">18:45 · ✓ на ПК</small>
              </div>
            </div>
            <div className="chat-compose"><span>＋</span><p>Сообщение</p><b>➤</b></div>
          </div>
          <span className="annotation annotation-one">метка удаления <i>↙</i></span>
          <span className="annotation annotation-two">медиа сохранено <i>↖</i></span>
        </div>
      </section>

      <section className="secret-section">
        <div className="secret-orbit" aria-hidden="true"><span>ά</span><i /><i /><i /></div>
        <div className="secret-copy">
          <div className="section-kicker"><span>03 / Секретно</span><i /></div>
          <h2>Секретный чат,<br />теперь и на компьютере.</h2>
          <p>
            Отдельное сквозное шифрование, отпечаток ключа, таймер и файлы — в том же
            удобном интерфейсе. Такой чат привязан к конкретному ПК и не переносится с телефона.
          </p>
          <div className="privacy-note"><span>✦</span><p>Самоуничтожение и медиа одного просмотра не архивируются.</p></div>
        </div>
      </section>

      <section className="download-section" id="download">
        <div className="download-intro">
          <div className="section-kicker light"><span>04 / Загрузка</span><i /></div>
          <h2>Выберите свою<br />платформу.</h2>
          <p>Проверенные сборки публикуются в GitHub Releases с контрольными суммами.</p>
        </div>

        <div className="download-grid">
          <article className="download-card featured">
            <div className="download-heading"><span className="os-mark">⊞</span><span><small>Desktop</small><strong>Windows</strong></span></div>
            <p>Portable-сборка x64. Запускается из отдельной папки и сохраняет данные рядом с клиентом.</p>
            <dl><div><dt>Версия</dt><dd>7.1.2 · a‑gram.15</dd></div><div><dt>Размер</dt><dd>119.4 МБ</dd></div><div><dt>Формат</dt><dd>ZIP · x64</dd></div></dl>
            <a className="download-button" href="https://github.com/oxyderkes/--Gram/releases/download/v2026.08.31/agram-windows-x64-7.1.2-a-gram.15-portable.zip">Скачать для Windows <span>↓</span></a>
            <code>SHA‑256 · 89FCCA…C790765</code>
          </article>

          <article className="download-card">
            <div className="download-heading"><span className="os-mark android">A</span><span><small>Mobile</small><strong>Android</strong></span></div>
            <p>APK для современных ARM64-устройств, включая Google Pixel с GrapheneOS.</p>
            <dl><div><dt>Версия</dt><dd>12.10.1 · a‑gram.5</dd></div><div><dt>Размер</dt><dd>42.6 МБ</dd></div><div><dt>Формат</dt><dd>APK · arm64</dd></div></dl>
            <a className="download-button secondary" href="https://github.com/oxyderkes/--Gram/releases/download/v2026.08.31/agram-android-arm64-12.10.1-a-gram.5.apk">Скачать APK <span>↓</span></a>
            <code>SHA‑256 · CB6E2E…0B322D</code>
          </article>
        </div>
        <p className="install-note"><span>!</span> Windows и Android могут предупредить о неизвестном издателе: сборки не имеют коммерческой цифровой подписи.</p>
      </section>

      <section className="faq-section">
        <div className="faq-title"><span>Нужно знать</span><h2>Коротко о важном.</h2></div>
        <div className="faq-list">
          {faqs.map(([question, answer], index) => (
            <details key={question} open={index === 0}>
              <summary><span>{String(index + 1).padStart(2, '0')}</span>{question}<b>＋</b></summary>
              <p>{answer}</p>
            </details>
          ))}
        </div>
      </section>

      <footer>
        <div className="footer-brand"><span className="brand-mark">ά</span><h2>Gram</h2></div>
        <p>Знакомая связь.<br />Больше контроля.</p>
        <div className="footer-links"><a href="#features">Возможности</a><a href="#privacy">Приватность</a><a href="#download">Загрузки</a></div>
        <div className="footer-meta"><span>Неофициальный клиент Telegram</span><span>© 2026 ά‑Gram</span><a href="#top">Наверх ↑</a></div>
      </footer>
    </main>
  );
}
