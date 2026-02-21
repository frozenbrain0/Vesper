source .venv/bin/activate
bash -c 'sleep 1; xdg-open http://localhost:8000' &
sphinx-autobuild source build/html
