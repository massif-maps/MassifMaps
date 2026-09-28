from lib import get, layer


def layers(v):
    return [
        layer('background', 'background', paint={'background-color': '#f7f5f0'}),
        layer('landcover', 'fill', 'landcover',
              filter=['in', get('class'), ['literal', ['wood', 'grass', 'farmland']]],
              paint={'fill-color': ['match', get('class'), 'wood', '#dfe7d5', 'grass', '#e7edda', '#efeee2'],
                     'fill-opacity': 0.8}),
        layer('water', 'fill', 'water', filter=['!=', get('brunnel'), 'tunnel'],
              paint={'fill-color': '#a8ccdf'}),
    ]
