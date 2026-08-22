import React from 'react';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import CodeBlock from '@theme/CodeBlock';

// The SAME manifest the Android gallery reads, generated from the examples' @ExampleInfo
// annotations by scripts/gen-examples.py. Adding an example file is the whole job: it appears
// here on the next build.
import manifest from '@site/../docs/examples/examples.json';

// The screenshots live in the docs tree next to the manifest, so they are browsable on GitHub and
// shipped inside the APK as well. require.context is how a data-driven list reaches them - a
// plain require() of a computed path does not resolve.
const shots = require.context('@site/../docs/examples/screenshots', false, /\.png$/);

const GITHUB = 'https://github.com/massif-maps/MassifMaps/blob/master/';

function shotFor(example) {
  try {
    // Webpack's asset modules hand back { default: url }, not the url itself.
    const asset = shots('./' + example.id + '.png');
    return asset && asset.default ? asset.default : asset;
  } catch (e) {
    return null;
  }
}

function Example({example}) {
  const [open, setOpen] = React.useState(false);
  const shot = shotFor(example);
  const source = example.code;
  return (
    <div className="card margin-bottom--md" style={{overflow: 'hidden'}}>
      {shot && (
        <div className="card__image">
          <img src={shot} alt={example.title} style={{width: '100%', display: 'block'}} />
        </div>
      )}
      <div className="card__body">
        <h3>{example.title}</h3>
        <p>{example.description}</p>
      </div>
      <div className="card__footer">
        <button
          className="button button--secondary button--sm margin-right--sm"
          onClick={() => setOpen(!open)}>
          {open ? 'Hide code' : 'Show code'}
        </button>
        <Link className="button button--link button--sm" to={GITHUB + example.source}>
          View on GitHub
        </Link>
        {open && (
          source
            ? <CodeBlock language="java">{source}</CodeBlock>
            : <p><em>Source not found — run scripts/gen-examples.py.</em></p>
        )}
      </div>
    </div>
  );
}

export default function Examples() {
  return (
    <Layout
      title="Examples"
      description="Android examples for the Massif Maps SDK, each one a single file.">
      <main className="container margin-vert--lg">
        <h1>Examples</h1>
        <p>
          Every example below is one self-contained file in the Android demo app, written against
          the <Link to="/docs/internals/api-facade">new facade API</Link>. The screenshots are
          captured from the app itself, so what you see is what it renders.
        </p>
        {manifest.sections.map((section) => (
          <section key={section.id} className="margin-top--lg">
            <h2>{section.title}</h2>
            <p>{section.description}</p>
            <div className="row">
              {section.examples.map((example) => (
                <div className="col col--4" key={example.id}>
                  <Example example={example} />
                </div>
              ))}
            </div>
          </section>
        ))}
      </main>
    </Layout>
  );
}
