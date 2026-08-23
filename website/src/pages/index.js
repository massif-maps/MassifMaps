import clsx from 'clsx';
import Link from '@docusaurus/Link';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import Layout from '@theme/Layout';
import HomepageFeatures from '@site/src/components/HomepageFeatures';
import {Platforms, StatusLabels} from '@site/src/data/platforms';
import styles from './index.module.css';

function HomepagePlatforms() {
  return (
    <section className="featureSection">
      <div className="container">
        <h2 style={{textAlign: 'center', marginBottom: '0.4rem'}}>Runs where your app runs</h2>
        <p style={{textAlign: 'center', marginBottom: '2rem', color: 'var(--ifm-color-emphasis-700)'}}>
          One C++ core, native bindings per platform.
        </p>
        <div className="platformStrip">
          {Platforms.filter((p) => p.status !== 'legacy').map((p) => (
            <Link key={p.id} to="/platforms" className="platformChip">
              <span className="platformChipIcon">{p.icon}</span>
              <span className="platformChipName">{p.name}</span>
              <span className={`statusBadge ${StatusLabels[p.status].className}`}>
                {StatusLabels[p.status].label}
              </span>
            </Link>
          ))}
        </div>
        <p style={{textAlign: 'center', marginTop: '1.5rem'}}>
          <Link to="/platforms">Full support matrix →</Link>
        </p>
      </div>
    </section>
  );
}

function HomepageHeader() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <header className="heroBanner">
      <div className="container">
        <h1 className="heroTitle">{siteConfig.title}</h1>
        <p className="heroSubtitle">{siteConfig.tagline}</p>
        <div className="heroButtons">
          <Link className="button button--lg button--hero" to="/docs/intro">
            Get Started
          </Link>
          <Link
            className="button button--lg button--hero-outline"
            to="/docs/features/3d-terrain">
            Explore Features
          </Link>
          <Link
            className="button button--lg button--hero-outline"
            to="/docs/migration">
            Coming from CARTO?
          </Link>
        </div>
      </div>
    </header>
  );
}

export default function Home() {
  const {siteConfig} = useDocusaurusContext();
  return (
    <Layout
      title={`${siteConfig.title} — Documentation`}
      description="Documentation for Massif Maps: installation, guides, feature docs and API reference for Android, iOS and NativeScript.">
      <HomepageHeader />
      <main>
        <HomepagePlatforms />
        <HomepageFeatures />
      </main>
    </Layout>
  );
}
