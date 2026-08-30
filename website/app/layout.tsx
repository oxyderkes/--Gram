import type { Metadata } from 'next';
import './globals.css';

const siteUrl = process.env.NEXT_PUBLIC_SITE_URL ?? 'http://localhost:3000';

export const metadata: Metadata = {
  metadataBase: new URL(siteUrl),
  title: 'ά‑Gram — Telegram без тесных рамок',
  description: 'Неофициальный клиент Telegram для Windows и Android: больше аккаунтов, локальная история и секретные чаты на компьютере.',
  icons: { icon: '/agram-logo.svg' },
  openGraph: {
    title: 'ά‑Gram — связь без тесных рамок',
    description: 'Больше аккаунтов, локальная история и секретные чаты на компьютере.',
    type: 'website',
    locale: 'ru_RU',
    images: [{ url: '/og.png', width: 1200, height: 630, alt: 'ά‑Gram — связь без тесных рамок' }],
  },
  twitter: {
    card: 'summary_large_image',
    title: 'ά‑Gram — связь без тесных рамок',
    description: 'Больше аккаунтов, локальная история и секретные чаты на компьютере.',
    images: ['/og.png'],
  },
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="ru">
      <body>{children}</body>
    </html>
  );
}
