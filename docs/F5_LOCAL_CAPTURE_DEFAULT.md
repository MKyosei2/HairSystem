# Visual Studio F5 local capture default

For `Debug | x64` and `Release | x64`, Visual Studio F5 now starts the Viewer with:

```text
--local-capture --local-capture-warmup 120 --local-capture-frames 1800
```

The run automatically stops after the requested capture and writes diagnostic output under:

```text
results/committed/local_debug/<UTC timestamp>_<process id>/
```

Close the Viewer normally or allow the automatic frame limit to finish. Using Visual Studio's **Stop Debugging** button terminates the process immediately and can prevent final CSV/JSON/SVG postprocessing.

These files are diagnostic only. Formal thesis evidence must still be produced with `Run_Final_Runtime_Suite.bat` and `prenovelty-complete`.
